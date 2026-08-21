/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <optional>
#include <string>

#include "../translations.hpp"

namespace mako::cli::validate {

    struct Options {
        std::optional<std::string> config;
    };

    int run(const Options& opts, i18n::Lang lang = i18n::Lang::En);

}
