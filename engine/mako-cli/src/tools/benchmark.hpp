/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <optional>
#include <string>

#include "../translations.hpp"

namespace mako::cli::benchmark {

    struct Options {
        std::optional<std::string> dll;
        bool allow_fp16{false};
        int width{1920};
        int height{1080};

        float flow{1.0F};
        int multiplier{2};
        bool performance_mode{false};
        std::optional<std::string> gpu;

        int duration{10};
    };

    int run(const Options& opts, i18n::Lang lang = i18n::Lang::En);

}
