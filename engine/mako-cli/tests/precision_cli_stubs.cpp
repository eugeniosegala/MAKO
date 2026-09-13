/* SPDX-License-Identifier: GPL-3.0-or-later */

// Link the real CLI parser to tool-entry probes so defaults and explicit
// precision choices can be checked without initializing Vulkan or a model.
#include "tools/benchmark.hpp"
#include "tools/debug.hpp"
#include "tools/inspect_dll.hpp"
#include "tools/quality.hpp"
#include "tools/validate.hpp"

#include <iostream>

namespace {
    int reportPrecision(const bool allowFp16) {
        std::cout << (allowFp16 ? "fp16-allowed" : "fp32") << '\n';
        return 0;
    }
}

namespace mako::cli {
    int benchmark::run(const Options& opts, i18n::Language) {
        return reportPrecision(opts.allow_fp16);
    }
    int debug::run(const Options& opts, i18n::Language) {
        return reportPrecision(opts.allow_fp16);
    }
    int quality::run(const Options& opts) {
        return reportPrecision(opts.allow_fp16);
    }
    int quality::runCombined(const CombinedOptions& opts) {
        return reportPrecision(opts.allow_fp16);
    }

    // Commands without LSFG precision controls must never reach these probes.
    int quality::runSpatial(const SpatialOptions&) { return 2; }
    int quality::runSpatialProfile(const SpatialProfileOptions&) { return 2; }
    int quality::runSynchronizationCanary(const SynchronizationCanaryOptions&) { return 2; }
    int inspect_dll::run(const Options&) { return 2; }
    int validate::run(const Options&, i18n::Language) { return 2; }
}
