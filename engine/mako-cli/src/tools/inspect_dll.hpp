/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <filesystem>

namespace mako::cli::inspect_dll {

    struct Options {
        std::filesystem::path dll;
    };

    int run(const Options& options);

}
