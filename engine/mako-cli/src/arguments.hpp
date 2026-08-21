/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "i18n.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace mako::cli {

    struct GlobalArguments {
        i18n::Language language{i18n::Language::English};
        size_t command_index{0};
        bool show_help{false};
    };

    using GlobalArgumentResult = std::variant<GlobalArguments, std::string>;

    [[nodiscard]] GlobalArgumentResult parse_global_arguments(
        std::span<const std::string_view> arguments
    );

}
