/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "arguments.hpp"
#include "i18n.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>

using namespace mako::cli;

namespace {
    void require(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "cli i18n test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    template<size_t Size>
    GlobalArgumentResult parse(const std::array<std::string_view, Size>& args) {
        return parse_global_arguments(std::span<const std::string_view>{args});
    }

    const GlobalArguments& require_success(const GlobalArgumentResult& result) {
        require(std::holds_alternative<GlobalArguments>(result),
            "argument parsing unexpectedly failed");
        return std::get<GlobalArguments>(result);
    }

    void require_complete(const i18n::Strings& strings) {
        require(!strings.validation_success.empty(), "validation success is empty");
        require(!strings.validation_missing_file.empty(), "missing-file message is empty");
        require(!strings.validation_failed.empty(), "validation failure is empty");
        require(!strings.benchmark_results.empty(), "benchmark heading is empty");
        require(!strings.benchmark_iterations.empty(), "benchmark iterations is empty");
        require(!strings.benchmark_generated_frames.empty(), "generated frames is empty");
        require(!strings.benchmark_total_frames.empty(), "total frames is empty");
        require(!strings.benchmark_generated_fps.empty(), "generated FPS is empty");
        require(!strings.benchmark_total_fps.empty(), "total FPS is empty");
        require(!strings.benchmark_seconds.empty(), "benchmark suffix is empty");
        require(!strings.error.empty(), "error prefix is empty");
        require(!strings.flow_scale_range.empty(), "flow-scale error is empty");
        require(!strings.multiplier_minimum.empty(), "multiplier error is empty");
        require(!strings.dimensions_positive.empty(), "dimension error is empty");
        require(!strings.duration_positive.empty(), "duration error is empty");
        require(!strings.gpu_not_found.empty(), "GPU error is empty");
        require(!strings.debug_path_missing.empty(), "debug path error is empty");
        require(!strings.debug_filename_invalid.empty(), "debug filename error is empty");
        require(!strings.frame_wait_failed.empty(), "frame wait error is empty");
        require(!strings.debug_image_open_failed.empty(), "image open error is empty");
        require(!strings.debug_image_read_failed.empty(), "image read error is empty");
    }
}

int main() {
    for (const std::string_view code : i18n::supported_language_codes) {
        const auto language = i18n::language_from_code(code);
        require(language.has_value(), "supported language code was rejected");
        require(i18n::code(*language) == code, "language code did not round-trip");
        require_complete(i18n::strings(*language));
    }
    require(!i18n::language_from_code("invalid"),
        "unsupported language silently fell back to English");

    const auto default_result = parse(std::array<std::string_view, 1>{"validate"});
    const auto& default_args = require_success(default_result);
    require(default_args.language == i18n::Language::English,
        "default language is not English");
    require(default_args.command_index == 0, "default command index changed");

    const auto separated_result = parse(
        std::array<std::string_view, 3>{"--lang", "pt-BR", "validate"}
    );
    const auto& separated_args = require_success(separated_result);
    require(separated_args.language == i18n::Language::PortugueseBrazil,
        "separated language option selected the wrong language");
    require(separated_args.command_index == 2,
        "separated language option selected the wrong command");

    const auto joined_result = parse(
        std::array<std::string_view, 2>{"--lang=es", "benchmark"}
    );
    const auto& joined_args = require_success(joined_result);
    require(joined_args.language == i18n::Language::Spanish,
        "joined language option selected the wrong language");
    require(joined_args.command_index == 1,
        "joined language option selected the wrong command");

    const auto help_result = parse(std::array<std::string_view, 1>{"--help"});
    require(require_success(help_result).show_help, "--help was not recognized");

    require(std::holds_alternative<std::string>(
        parse(std::array<std::string_view, 1>{"--lang"})
    ), "missing --lang value was accepted");
    require(std::holds_alternative<std::string>(
        parse(std::array<std::string_view, 3>{"--lang", "invalid", "validate"})
    ), "invalid language was accepted");
    require(std::holds_alternative<std::string>(
        parse(std::array<std::string_view, 0>{})
    ), "missing command was accepted");

    const auto subcommand_result = parse(
        std::array<std::string_view, 3>{"validate", "--lang", "es"}
    );
    require(require_success(subcommand_result).command_index == 0,
        "global parser consumed a subcommand option");

    std::cout << "cli i18n tests: PASS\n";
    return EXIT_SUCCESS;
}
