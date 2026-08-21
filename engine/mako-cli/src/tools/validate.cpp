/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "validate.hpp"
#include "mako-common/configuration/config.hpp"
#include "../translations.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

using namespace mako::cli;
using namespace mako::cli::validate;

int validate::run(const Options& opts, i18n::Lang lang) {
    const auto& s = i18n::get(lang);

    std::filesystem::path path{ls::findConfigurationFile()};
    if (opts.config.has_value())
        path = *opts.config;

    if (!std::filesystem::exists(path)) {
        std::cerr << s.validateFailNoFile << '\n';
        return 1;
    }

    try {
        const ls::ConfigFile config{path};
        std::cerr << s.validateSuccess << '\n';
    } catch (const std::exception& e) {
        std::cerr << s.validateFailError << e.what() << '\n';
        return 1;
    }
    return 0;
}
