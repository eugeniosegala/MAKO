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
    };

    /// run the deterministic GPU image-quality regression
    int run(const Options& opts);
}
