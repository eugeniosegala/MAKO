/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <optional>
#include <string>

namespace mako::cli::i18n {
    enum class Language;
}

namespace mako::cli::benchmark {

    /// options for the "benchmark" command
    struct Options {
        std::optional<std::string> dll;
        bool allow_fp16{true};
        int width{1920};
        int height{1080};

        float flow{1.0F};
        int multiplier{2};
        bool performance_mode{false};
        std::optional<std::string> gpu;

        int duration{10};

        // Separate diagnostic recipe; never used by ordinary capacity runs.
        bool profile{false};
        int profile_samples{200};
        int profile_warmup{200};
        bool profile_hdr10{false};
    };

    /// run the "benchmark" command
    /// @param opts the command options
    int run(const Options& opts, i18n::Language language);

}
