/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-backend/dll_inspection.hpp"

#include "dll_reader.hpp"
#include "model_resource_validation.hpp"

#include <exception>
#include <functional>
#include <string>

namespace {
    [[nodiscard]] mako::backend::ModelCompatibility inspectCapability(
            const std::function<void()>& inspect) {
        try {
            inspect();
            return {.compatible = true, .reason = {}};
        } catch (const std::exception& error) {
            return {.compatible = false, .reason = error.what()};
        }
    }
}

mako::backend::LosslessDllInspection mako::backend::inspectLosslessDll(
        const std::filesystem::path& dll) {
    const auto archive = loadDllResourceArchive(dll);
    const auto lsfg = [&archive](const bool fp16, const bool performance) {
        return inspectCapability([&archive, fp16, performance] {
            detail::validateLsfgModelResources(
                archive->resources, fp16, performance
            );
        });
    };
    const auto ls1 = [&archive](const Ls1Mode mode) {
        return inspectCapability([&archive, mode] {
            detail::validateLs1ModelResources(archive->resources, mode);
        });
    };
    return {
        .fileSha256 = archive->fileSha256,
        .resourceLayoutSha256 = archive->resourceLayoutSha256,
        .fileSize = archive->fileSize,
        .resourceCount = archive->resources.size(),
        .lsfgFp32Quality = lsfg(false, false),
        .lsfgFp32Performance = lsfg(false, true),
        .lsfgFp16Quality = lsfg(true, false),
        .lsfgFp16Performance = lsfg(true, true),
        .ls1Quality = ls1(Ls1Mode::Quality),
        .ls1Performance = ls1(Ls1Mode::Performance),
    };
}
