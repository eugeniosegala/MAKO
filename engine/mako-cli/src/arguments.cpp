/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "arguments.hpp"

#include <string>

using namespace mako::cli;

namespace {
    constexpr std::string_view language_prefix{"--lang="};

    std::string unsupported_language(const std::string_view code) {
        return "unsupported language '" + std::string{code} +
            "'; expected en, pt-BR, pt-PT, or es";
    }
}

GlobalArgumentResult mako::cli::parse_global_arguments(
        const std::span<const std::string_view> arguments) {
    GlobalArguments result{};
    size_t index{0};

    while (index < arguments.size()) {
        const std::string_view argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            result.show_help = true;
            result.command_index = index;
            return result;
        }
        if (argument == "--") {
            ++index;
            break;
        }
        if (argument == "--lang") {
            if ((index + 1) >= arguments.size())
                return std::string{"--lang requires en, pt-BR, pt-PT, or es"};
            const std::string_view value = arguments[index + 1];
            const auto language = i18n::language_from_code(value);
            if (!language)
                return unsupported_language(value);
            result.language = *language;
            index += 2;
            continue;
        }
        if (argument.starts_with(language_prefix)) {
            const std::string_view value = argument.substr(language_prefix.size());
            const auto language = i18n::language_from_code(value);
            if (!language)
                return unsupported_language(value);
            result.language = *language;
            ++index;
            continue;
        }
        if (argument.starts_with('-'))
            return "unknown global option '" + std::string{argument} + "'";
        break;
    }

    if (index >= arguments.size())
        return std::string{"missing command"};

    result.command_index = index;
    return result;
}
