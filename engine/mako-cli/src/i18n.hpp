/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace mako::cli::i18n {

    enum class Language {
        English,
        PortugueseBrazil,
        PortuguesePortugal,
        Spanish,
    };

    inline constexpr std::array supported_language_codes{
        std::string_view{"en"},
        std::string_view{"pt-BR"},
        std::string_view{"pt-PT"},
        std::string_view{"es"},
    };

    struct Strings {
        std::string_view validation_success;
        std::string_view validation_missing_file;
        std::string_view validation_failed;
        std::string_view benchmark_results;
        std::string_view benchmark_iterations;
        std::string_view benchmark_generated_frames;
        std::string_view benchmark_total_frames;
        std::string_view benchmark_generated_fps;
        std::string_view benchmark_total_fps;
        std::string_view benchmark_seconds;
        std::string_view error;
        std::string_view flow_scale_range;
        std::string_view multiplier_minimum;
        std::string_view dimensions_positive;
        std::string_view duration_positive;
        std::string_view gpu_not_found;
        std::string_view debug_path_missing;
        std::string_view debug_filename_invalid;
        std::string_view frame_wait_failed;
        std::string_view debug_image_open_failed;
        std::string_view debug_image_read_failed;
    };

    [[nodiscard]] const Strings& strings(Language language) noexcept;
    [[nodiscard]] std::optional<Language> language_from_code(
        std::string_view code
    ) noexcept;
    [[nodiscard]] std::string_view code(Language language) noexcept;

}
