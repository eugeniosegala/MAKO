/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "inspect_dll.hpp"

#include "mako-backend/dll_inspection.hpp"
#include "mako-backend/ls1.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace {
    void printStatus(const std::string& name,
            const mako::backend::ModelCompatibility& status) {
        std::cout << "  " << name << ": "
                  << (status.compatible ? "compatible" : "unsupported");
        if (!status.reason.empty())
            std::cout << " (" << status.reason << ')';
        std::cout << '\n';
    }

    [[nodiscard]] mako::backend::ModelCompatibility inspectTranslation(
            const std::filesystem::path& dll,
            const mako::backend::Ls1Mode mode) {
        try {
            for (uint32_t variant = 0; variant < 5U; ++variant) {
                static_cast<void>(mako::backend::loadLs1ShaderSet(
                    dll, mode, static_cast<float>(variant) / 4.0F
                ));
            }
            return {.compatible = true, .reason = {}};
        } catch (const std::exception& error) {
            return {.compatible = false, .reason = error.what()};
        }
    }
}

int mako::cli::inspect_dll::run(const Options& options) {
    try {
        const auto inspection = mako::backend::inspectLosslessDll(options.dll);
        const auto qualityTranslation = inspectTranslation(
            options.dll, mako::backend::Ls1Mode::Quality
        );
        const auto performanceTranslation = inspectTranslation(
            options.dll, mako::backend::Ls1Mode::Performance
        );
        std::cout << "MAKO model DLL inspection\n"
                  << "  dll_sha256: " << inspection.fileSha256 << '\n'
                  << "  resource_layout_sha256: "
                  << inspection.resourceLayoutSha256 << '\n'
                  << "  file_size: " << inspection.fileSize << '\n'
                  << "  resource_count: " << inspection.resourceCount << '\n';
        printStatus("LSFG FP32 Quality", inspection.lsfgFp32Quality);
        printStatus("LSFG FP32 Performance", inspection.lsfgFp32Performance);
        printStatus("LSFG FP16 Quality", inspection.lsfgFp16Quality);
        printStatus("LSFG FP16 Performance", inspection.lsfgFp16Performance);
        printStatus("LS1 Quality resources", inspection.ls1Quality);
        printStatus("LS1 Performance resources", inspection.ls1Performance);
        printStatus("LS1 Quality translation", qualityTranslation);
        printStatus("LS1 Performance translation", performanceTranslation);

        const bool coreCompatible =
            inspection.lsfgFp32Quality.compatible &&
            inspection.lsfgFp32Performance.compatible &&
            inspection.ls1Quality.compatible &&
            inspection.ls1Performance.compatible &&
            qualityTranslation.compatible && performanceTranslation.compatible;
        std::cout << "  result: "
                  << (coreCompatible ? "compatible" : "partially compatible")
                  << '\n';
        return coreCompatible ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "MAKO model DLL inspection failed: " << error.what() << '\n';
        return 1;
    }
}
