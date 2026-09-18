/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "tools/inspect_dll.hpp"
#include "mako-backend/dll_inspection.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
    bool fp32Compatible = true;
    bool fp16Compatible = true;
    std::vector<bool> checkedPrecisions;
}

// Exercise the actual CLI protocol and precision policy without licensed data
// or Vulkan. Backend resource validation has its own synthetic archive tests.
namespace mako::backend {
    ModelCompatibility inspectLsfgRegistry(const std::filesystem::path&, bool fp16) {
        checkedPrecisions.push_back(fp16);
        return {.compatible = fp16 ? fp16Compatible : fp32Compatible, .reason = {}};
    }
    LosslessDllInspection inspectLosslessDll(const std::filesystem::path&) {
        throw std::runtime_error("LSFG-only inspection must not inspect LS1");
    }
    Ls1ShaderSet loadLs1ShaderSet(const std::filesystem::path&, Ls1Mode, float) {
        throw std::runtime_error("LSFG-only inspection must not translate LS1");
    }
}

int main() {
    for (const bool allowFp16 : {false, true})
        for (const bool fp32 : {false, true})
            for (const bool fp16 : {false, true}) {
                fp32Compatible = fp32;
                fp16Compatible = fp16;
                checkedPrecisions.clear();
                std::ostringstream output;
                auto* previous = std::cout.rdbuf(output.rdbuf());
                const int status = mako::cli::inspect_dll::run({
                    .dll = "synthetic", .lsfg = true, .allowFp16 = allowFp16,
                });
                std::cout.rdbuf(previous);
                const bool compatible = fp32 && (!allowFp16 || fp16);
                const std::string expected = std::string("{\"schema_version\":1,\"compatible\":") +
                    (compatible ? "true" : "false") + "}\n";
                const std::vector<bool> expectedPrecisions = allowFp16
                    ? std::vector<bool>{false, true} : std::vector<bool>{false};
                if (status != (compatible ? 0 : 1) || output.str() != expected ||
                        checkedPrecisions != expectedPrecisions) {
                    std::cerr << "LSFG inspection protocol or precision policy mismatch\n";
                    return 1;
                }
            }
    return 0;
}
