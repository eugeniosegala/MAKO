/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

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
