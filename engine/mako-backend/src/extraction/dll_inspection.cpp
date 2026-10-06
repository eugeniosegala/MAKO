/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-backend/dll_inspection.hpp"

#include "dll_reader.hpp"
#include "lsfg_shader_set.hpp"

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

mako::backend::ModelCompatibility mako::backend::inspectLsfgRegistry(
        const std::filesystem::path& dll, const bool fp16) {
    return inspectCapability([&] {
        const auto archive = loadDllResourceArchive(dll);
        static_cast<void>(loadLsfgShaderSet(*archive, dll, fp16));
    });
}

mako::backend::LosslessDllInspection mako::backend::inspectLosslessDll(
        const std::filesystem::path& dll) {
    const auto archive = loadDllResourceArchive(dll);
    const auto lsfg = [&archive, &dll](const bool fp16, const bool performance) {
        try {
            const auto shaders = loadLsfgShaderSet(*archive, dll, fp16, performance);
            if (fp16 && !shaders.fp16)
                return ModelCompatibility{.compatible = false,
                    .reason = "DirectX fallback uses FP32; FP16 is unavailable"};
            return ModelCompatibility{.compatible = true,
                .reason = shaders.convertedFp16Stages
                    ? "Experimental forced FP16 (image quality unqualified)"
                    : (shaders.translated ? "DirectX translation (FP32)" : "")};
        } catch (const std::exception& error) {
            return ModelCompatibility{.compatible = false, .reason = error.what()};
        }
    };
    const auto ls1 = [&archive](const Ls1Mode mode) {
        return inspectCapability([&archive, mode] {
            static_cast<void>(resolveLs1ModelResources(*archive, mode));
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
