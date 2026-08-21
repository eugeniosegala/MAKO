/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "validate.hpp"
#include "i18n.hpp"
#include "mako-common/configuration/config.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

using namespace mako::cli;
using namespace mako::cli::validate;

int validate::run(const Options& opts, const i18n::Language language) {
    const i18n::Strings& text = i18n::strings(language);
    std::filesystem::path path{ls::findConfigurationFile()};
    if (opts.config.has_value())
        path = *opts.config;

    if (!std::filesystem::exists(path)) {
        std::cerr << text.validation_missing_file << '\n';
        return 1;
    }

    try {
        const ls::ConfigFile config{path};
        std::cerr << text.validation_success << '\n';
    } catch (const std::exception& e) {
        std::cerr << text.validation_failed << e.what() << '\n';
        return 1;
    }
    return 0;
}
