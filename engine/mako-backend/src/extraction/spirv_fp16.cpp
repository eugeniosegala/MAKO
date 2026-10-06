/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "spirv_fp16.hpp"
#include "mako-common/helpers/errors.hpp"

#ifdef MAKO_EXPERIMENTAL_LSFG_FP16
#include <spirv-tools/optimizer.hpp>

#include <cstring>
#include <set>

namespace {
    bool hasHalfArithmetic(const std::vector<uint32_t>& words) {
        std::set<uint32_t> halfTypes;
        for (size_t index = 5; index < words.size();) {
            const auto count = words[index] >> 16;
            const auto opcode = words[index] & 0xffffU;
            if (count == 3 && opcode == 22 && words[index + 2] == 16)
                halfTypes.insert(words[index + 1]); // OpTypeFloat
            if (count == 4 && opcode == 23 && halfTypes.contains(words[index + 2]))
                halfTypes.insert(words[index + 1]); // OpTypeVector
            // Arithmetic result types, excluding mere FP16 conversion/storage.
            if (count >= 4 && halfTypes.contains(words[index + 1]) &&
                    (opcode == 12 || opcode == 127 || opcode == 129 ||
                     opcode == 131 || opcode == 133 || opcode == 136 ||
                     opcode == 140 || opcode == 141 || opcode == 142 || opcode == 148))
                return true;
            index += count;
        }
        return false;
    }
}
#endif

bool mako::backend::detail::experimentalLsfgFp16Available() {
#ifdef MAKO_EXPERIMENTAL_LSFG_FP16
    return true;
#else
    return false;
#endif
}

mako::backend::detail::ConvertedFp16Shader
mako::backend::detail::convertLsfgShaderToFp16(
        const std::span<const uint8_t> source,
        const ShaderResourceContract& contract, const std::string& sourceName) {
#ifdef MAKO_EXPERIMENTAL_LSFG_FP16
    static_cast<void>(validateSpirvComputeShader(source, contract, sourceName));
    std::vector<uint32_t> words(source.size() / sizeof(uint32_t));
    std::memcpy(words.data(), source.data(), source.size());
    spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_1);
    // Optimizer diagnostics can contain licensed instructions. Report only
    // the stage identifier, never a module fragment or a filesystem dump.
    optimizer.SetMessageConsumer([](auto, const char*, const spv_position_t&, const char*) {});
    optimizer.RegisterPerformancePasses(true);
    optimizer.RegisterPass(spvtools::CreateRelaxFloatOpsPass());
    optimizer.RegisterPass(spvtools::CreateConvertRelaxedToHalfPass());
    optimizer.RegisterPass(spvtools::CreateSimplificationPass());
    optimizer.RegisterPass(spvtools::CreateRedundancyEliminationPass());
    optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass(true));
    spvtools::OptimizerOptions options;
    options.set_preserve_bindings(true);
    std::vector<uint32_t> result;
    if (!optimizer.Run(words.data(), words.size(), &result, options))
        throw ls::error(sourceName + ": experimental FP16 conversion failed validation");
    spvtools::SpirvTools validator(SPV_ENV_VULKAN_1_1);
    validator.SetMessageConsumer([](auto, const char*, const spv_position_t&, const char*) {});
    if (!validator.Validate(result))
        throw ls::error(sourceName + ": converted FP16 module failed validation");
    ConvertedFp16Shader converted{.bytes = std::vector<uint8_t>(result.size() * sizeof(uint32_t)),
        .hasHalfArithmetic = hasHalfArithmetic(result)};
    std::memcpy(converted.bytes.data(), result.data(), converted.bytes.size());
    static_cast<void>(validateSpirvComputeShader(converted.bytes, contract, sourceName));
    return converted;
#else
    static_cast<void>(source);
    static_cast<void>(contract);
    throw ls::error(sourceName + ": experimental FP16 conversion is not built");
#endif
}
