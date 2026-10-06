/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "spirv_fp16.hpp"
#include "mako-common/helpers/errors.hpp"

#ifdef MAKO_EXPERIMENTAL_LSFG_FP16
#include <spirv-tools/optimizer.hpp>
#include <spirv/unified1/spirv.hpp11>

#include <cstring>
#include <set>
#include <unordered_map>

namespace {
    struct ValueDependencies {
        spv::Op opcode;
        std::vector<uint32_t> inputs;
    };

    struct PrecisionDependencies {
        std::unordered_map<uint32_t, ValueDependencies> values;
        std::set<uint32_t> exact;
        std::vector<std::pair<uint32_t, uint32_t>> stores;
    };

    bool imageRead(const spv::Op opcode) {
        switch (opcode) {
            case spv::Op::OpImageSampleImplicitLod:
            case spv::Op::OpImageSampleExplicitLod:
            case spv::Op::OpImageSampleDrefImplicitLod:
            case spv::Op::OpImageSampleDrefExplicitLod:
            case spv::Op::OpImageSampleProjImplicitLod:
            case spv::Op::OpImageSampleProjExplicitLod:
            case spv::Op::OpImageSampleProjDrefImplicitLod:
            case spv::Op::OpImageSampleProjDrefExplicitLod:
            case spv::Op::OpImageFetch:
            case spv::Op::OpImageGather:
            case spv::Op::OpImageDrefGather:
            case spv::Op::OpImageRead:
                return true;
            default: return false;
        }
    }

    spv_result_t collectDependencies(void* user, const spv_parsed_instruction_t* inst) {
        auto& dependencies = *static_cast<PrecisionDependencies*>(user);
        const auto opcode = static_cast<spv::Op>(inst->opcode);
        ValueDependencies value{opcode, {}};
        if (opcode == spv::Op::OpStore)
            dependencies.stores.emplace_back(inst->words[1], inst->words[2]);
        for (uint16_t i = 0; i < inst->num_operands; ++i) {
            const auto& operand = inst->operands[i];
            if (operand.type != SPV_OPERAND_TYPE_ID) continue;
            const auto id = inst->words[operand.offset];
            value.inputs.push_back(id);
            // Sampling coordinates, LODs and offsets must not be rounded to
            // half before the image instruction expands them back to FP32.
            if ((imageRead(opcode) && operand.offset >= 4) ||
                    (opcode == spv::Op::OpImageWrite && operand.offset == 2) ||
                    opcode == spv::Op::OpBitcast)
                dependencies.exact.insert(id);
        }
        if (inst->result_id)
            dependencies.values.emplace(inst->result_id, std::move(value));
        return SPV_SUCCESS;
    }

    void preserveExactInputs(std::vector<uint32_t>& words, const std::string& sourceName) {
        PrecisionDependencies dependencies;
        const auto context = spvContextCreate(SPV_ENV_VULKAN_1_1);
        spv_diagnostic diagnostic = nullptr;
        const auto status = spvBinaryParse(context, &dependencies, words.data(), words.size(),
            nullptr, collectDependencies, &diagnostic);
        spvDiagnosticDestroy(diagnostic);
        spvContextDestroy(context);
        if (status != SPV_SUCCESS)
            throw ls::error(sourceName + ": experimental FP16 dependency analysis failed");

        // Translated shaders can keep coordinates in private/function memory.
        // Following an OpLoad's pointer alone misses the arithmetic stored there.
        for (const auto [pointer, object] : dependencies.stores) {
            auto base = pointer;
            for (;;) {
                const auto value = dependencies.values.find(base);
                if (value == dependencies.values.end() || value->second.inputs.empty()) break;
                const auto opcode = value->second.opcode;
                if (opcode != spv::Op::OpAccessChain && opcode != spv::Op::OpInBoundsAccessChain &&
                        opcode != spv::Op::OpPtrAccessChain &&
                        opcode != spv::Op::OpInBoundsPtrAccessChain &&
                        opcode != spv::Op::OpCopyObject) break;
                base = value->second.inputs.front();
            }
            if (const auto value = dependencies.values.find(base); value != dependencies.values.end())
                value->second.inputs.push_back(object);
        }

        std::vector<uint32_t> pending(dependencies.exact.begin(), dependencies.exact.end());
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            const auto value = dependencies.values.find(id);
            if (value == dependencies.values.end() || imageRead(value->second.opcode)) continue;
            for (const auto input : value->second.inputs)
                if (dependencies.exact.insert(input).second) pending.push_back(input);
        }

        size_t output = 5;
        for (size_t index = 5; index < words.size();) {
            const auto count = words[index] >> 16;
            const auto opcode = static_cast<spv::Op>(words[index] & 0xffffU);
            if (!(opcode == spv::Op::OpDecorate && count == 3 &&
                    words[index + 2] == static_cast<uint32_t>(spv::Decoration::RelaxedPrecision) &&
                    dependencies.exact.contains(words[index + 1]))) {
                std::memmove(words.data() + output, words.data() + index, count * sizeof(uint32_t));
                output += count;
            }
            index += count;
        }
        words.resize(output);
    }

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
    spvtools::OptimizerOptions options;
    options.set_preserve_bindings(true);
    std::vector<uint32_t> relaxed;
    if (!optimizer.Run(words.data(), words.size(), &relaxed, options))
        throw ls::error(sourceName + ": experimental FP16 preparation failed validation");
    preserveExactInputs(relaxed, sourceName);
    spvtools::Optimizer converter(SPV_ENV_VULKAN_1_1);
    converter.SetMessageConsumer([](auto, const char*, const spv_position_t&, const char*) {});
    converter.RegisterPass(spvtools::CreateConvertRelaxedToHalfPass());
    converter.RegisterPass(spvtools::CreateSimplificationPass());
    converter.RegisterPass(spvtools::CreateRedundancyEliminationPass());
    converter.RegisterPass(spvtools::CreateAggressiveDCEPass(true));
    std::vector<uint32_t> result;
    if (!converter.Run(relaxed.data(), relaxed.size(), &result, options))
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
