/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "mako-common/configuration/config.hpp"
#include <exception>

namespace mako::layer {
    // Startup only: an unavailable optional model must not disable MAKO FG.
    // The caller retains the saved selection and records the returned applied model.
    template<typename Initialize, typename ReportFallback>
    ls::FrameGenerationModel initializeFrameGenerationBackend(
            ls::FrameGenerationModel requested, Initialize&& initialize,
            ReportFallback&& reportFallback) {
        try {
            initialize(requested);
            return requested;
        } catch (const std::exception& error) {
            if (requested != ls::FrameGenerationModel::LosslessScaling)
                throw;
            reportFallback(error);
            initialize(ls::FrameGenerationModel::MakoFg);
            return ls::FrameGenerationModel::MakoFg;
        }
    }
}
