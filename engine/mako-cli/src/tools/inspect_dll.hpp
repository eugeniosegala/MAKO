/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-backend/ls1.hpp"

#include <filesystem>
#include <optional>

namespace mako::cli::inspect_dll {

    struct Options {
        std::filesystem::path dll;
        std::optional<backend::Ls1Mode> ls1Mode;
        float sharpness{0.8F};
    };

    int run(const Options& options);

}
