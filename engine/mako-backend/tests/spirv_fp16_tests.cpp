/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "extraction/lsfg_shader_set.hpp"
#include "extraction/spirv_fp16.hpp"
#include "mako-common/helpers/errors.hpp"

#include <spirv-tools/libspirv.hpp>
#include <spirv/unified1/spirv.hpp11>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {
    using namespace mako::backend;

    void require(const bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }

    template<typename Action> void requireFailure(Action action) {
        try { action(); } catch (const ls::error&) { return; }
        throw std::runtime_error("invalid conversion was accepted");
    }

    // Original synthetic shader: no proprietary bytes or filesystem fixtures.
    // A live invocation-dependent float calculation writes every storage image;
    // unused descriptor bindings must survive optimizer cleanup as well.
    std::vector<uint8_t> shader(const detail::ShaderResourceContract& contract,
            const bool arithmetic = true, const bool exactInputs = false,
            const bool indexedMemory = false) {
        std::ostringstream text;
        text << "OpCapability Shader\nOpMemoryModel Logical GLSL450\n"
            << "OpEntryPoint GLCompute %main \"main\" %gid\n"
            << "OpExecutionMode %main LocalSize 8 8 1\n"
            << "OpDecorate %gid BuiltIn GlobalInvocationId\n"
            << "OpDecorate %block Block\nOpMemberDecorate %block 0 Offset 0\n";
        const auto decorate = [&](const char* prefix, const size_t count, const size_t base) {
            for (size_t i = 0; i < count; ++i)
                text << "OpDecorate %" << prefix << i << " DescriptorSet 0\n"
                    << "OpDecorate %" << prefix << i << " Binding " << base + i << '\n';
        };
        decorate("u", contract.uniformBuffers, 0);
        decorate("s", contract.samplers, 16);
        decorate("t", contract.sampledImages, 32);
        decorate("o", contract.storageImages, 48);
        text << R"(%void = OpTypeVoid
%fn = OpTypeFunction %void
%uint = OpTypeInt 32 0
%zero = OpConstant %uint 0
%one = OpConstant %uint 1
%two = OpConstant %uint 2
%float = OpTypeFloat 32
%v2u = OpTypeVector %uint 2
%v3u = OpTypeVector %uint 3
%v2f = OpTypeVector %float 2
%v4f = OpTypeVector %float 4
%input = OpTypePointer Input %v3u
%gid = OpVariable %input Input
%privateFloat = OpTypePointer Private %float
%savedPixel = OpVariable %privateFloat Private
%pair = OpTypeArray %float %two
%privatePair = OpTypePointer Private %pair
%savedPair = OpVariable %privatePair Private
%block = OpTypeStruct %float
%pu = OpTypePointer Uniform %block
%sampler = OpTypeSampler
%ps = OpTypePointer UniformConstant %sampler
%sampled = OpTypeImage %float 2D 0 0 0 1 Unknown
%sampledSampler = OpTypeSampledImage %sampled
%pt = OpTypePointer UniformConstant %sampled
%stored = OpTypeImage %float 2D 0 0 0 2 Rgba8
%po = OpTypePointer UniformConstant %stored
%quarter = OpConstant %float 0.25
%half = OpConstant %float 0.5
)";
        const auto variables = [&](const char* prefix, const char* pointer,
                const char* storage, const size_t count) {
            for (size_t i = 0; i < count; ++i)
                text << '%' << prefix << i << " = OpVariable %" << pointer << ' ' << storage << '\n';
        };
        variables("u", "pu", "Uniform", contract.uniformBuffers);
        variables("s", "ps", "UniformConstant", contract.samplers);
        variables("t", "pt", "UniformConstant", contract.sampledImages);
        variables("o", "po", "UniformConstant", contract.storageImages);
        text << "%main = OpFunction %void None %fn\n%entry = OpLabel\n"
            << "%id = OpLoad %v3u %gid\n%x = OpCompositeExtract %uint %id 0\n"
            << "%y = OpCompositeExtract %uint %id 1\n%coord = OpCompositeConstruct %v2u %x %y\n";
        if (arithmetic)
            text << "%f = OpConvertUToF %float %x\n%add = OpFAdd %float %f %quarter\n"
                << "%value = OpFMul %float %add %half\n";
        if (exactInputs) {
            text << "%pixel = OpFMul %float %f %quarter\n";
            if (indexedMemory)
                text << "%index = OpBitwiseAnd %uint %x %one\n"
                    << "%write = OpAccessChain %privateFloat %savedPair %index\n"
                    << "OpStore %write %pixel Volatile\n"
                    << "%read = OpAccessChain %privateFloat %savedPair %index\n"
                    << "%loadedPixel = OpLoad %float %read Volatile\n";
            else
                text << "OpStore %savedPixel %pixel Volatile\n"
                    << "%loadedPixel = OpLoad %float %savedPixel Volatile\n";
            text << R"(%uv = OpCompositeConstruct %v2f %loadedPixel %loadedPixel
%texture = OpLoad %sampled %t0
%filter = OpLoad %sampler %s0
%combined = OpSampledImage %sampledSampler %texture %filter
%sample = OpImageSampleExplicitLod %v4f %combined %uv Lod %quarter
%channel = OpCompositeExtract %float %sample 0
%bits = OpBitcast %uint %loadedPixel
%visibleBits = OpConvertUToF %float %bits
%colour = OpCompositeConstruct %v4f %value %channel %visibleBits %value
)";
        } else
            text << "%colour = OpCompositeConstruct %v4f "
                << (arithmetic ? "%value %value %value %value\n" : "%half %half %half %half\n");
        for (size_t i = 0; i < contract.storageImages; ++i)
            text << "%image" << i << " = OpLoad %stored %o" << i << '\n'
                << "OpImageWrite %image" << i << " %coord %colour\n";
        text << "OpReturn\nOpFunctionEnd\n";
        spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_1);
        tools.SetMessageConsumer([](auto, const char*, const spv_position_t&, const char* message) {
            std::cerr << message << '\n'; // Only this original synthetic module.
        });
        std::vector<uint32_t> words;
        require(tools.Assemble(text.str(), &words) && tools.Validate(words), "invalid synthetic shader");
        std::vector<uint8_t> result(words.size() * sizeof(uint32_t));
        std::memcpy(result.data(), words.data(), result.size());
        return result;
    }

    void requireExactInputs(const std::vector<uint8_t>& bytes, const bool requireMemory) {
        struct Value { uint32_t type; std::vector<uint32_t> inputs; };
        struct Parsed {
            std::unordered_map<uint32_t, Value> values;
            std::set<uint32_t> halfTypes;
            std::vector<uint32_t> exactRoots;
            size_t stores = 0;
        } parsed;
        std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
        std::memcpy(words.data(), bytes.data(), bytes.size());
        const auto context = spvContextCreate(SPV_ENV_VULKAN_1_1);
        const auto status = spvBinaryParse(context, &parsed, words.data(), words.size(), nullptr,
            [](void* user, const spv_parsed_instruction_t* inst) {
                auto& result = *static_cast<Parsed*>(user);
                const auto opcode = static_cast<spv::Op>(inst->opcode);
                if ((opcode == spv::Op::OpTypeFloat && inst->words[2] == 16) ||
                        (opcode == spv::Op::OpTypeVector && result.halfTypes.contains(inst->words[2])))
                    result.halfTypes.insert(inst->result_id);
                if (opcode == spv::Op::OpImageSampleExplicitLod)
                    result.exactRoots.push_back(inst->words[4]);
                if (opcode == spv::Op::OpBitcast) result.exactRoots.push_back(inst->words[3]);
                // This fixture stores only its sampling/encoded-bit value.
                // Check the stored calculation itself: an FP32 load cannot
                // restore precision lost before the store.
                if (opcode == spv::Op::OpStore) {
                    result.exactRoots.push_back(inst->words[2]);
                    ++result.stores;
                }
                Value value{inst->type_id, {}};
                for (uint16_t i = 0; i < inst->num_operands; ++i)
                    if (inst->operands[i].type == SPV_OPERAND_TYPE_ID)
                        value.inputs.push_back(inst->words[inst->operands[i].offset]);
                if (inst->result_id) result.values.emplace(inst->result_id, std::move(value));
                return SPV_SUCCESS;
            }, nullptr);
        spvContextDestroy(context);
        require(status == SPV_SUCCESS && parsed.exactRoots.size() >= 2 &&
                (!requireMemory || parsed.stores > 0),
            "sampling/bit-pattern fixture was optimized away");
        std::set<uint32_t> visited;
        while (!parsed.exactRoots.empty()) {
            const auto id = parsed.exactRoots.back();
            parsed.exactRoots.pop_back();
            if (!visited.insert(id).second) continue;
            const auto value = parsed.values.find(id);
            if (value == parsed.values.end()) continue;
            require(!parsed.halfTypes.contains(value->second.type),
                "half rounding changed a sample location or encoded float bits");
            parsed.exactRoots.insert(parsed.exactRoots.end(),
                value->second.inputs.begin(), value->second.inputs.end());
        }
    }

    DllResourceArchive graph(const bool arithmetic = true) {
        DllResourceArchive archive;
        for (const bool mode : {false, true})
            for (const auto& spec : detail::lsfgShaderSpecs(mode))
                archive.resources.emplace(detail::lsfgResourceId(spec.logicalId, false, mode),
                    shader(spec.contract, arithmetic));
        return archive;
    }

    void tests() {
        require(detail::experimentalLsfgFp16Available(), "converter absent in experimental build");
        const detail::ShaderResourceContract contract{2, 1, 1, 1};
        const auto original = shader(contract);
        const auto converted = detail::convertLsfgShaderToFp16(original, contract, "synthetic");
        const auto info = detail::validateSpirvComputeShader(converted.bytes, contract, "synthetic");
        require(info.exactBindings && info.declaresFloat16 && converted.hasHalfArithmetic,
            "conversion changed bindings or did not produce half arithmetic");
        for (const bool indexedMemory : {false, true}) {
            const auto mixed = detail::convertLsfgShaderToFp16(
                shader(contract, true, true, indexedMemory),
                contract, "sampling and bit-pattern synthetic");
            require(mixed.hasHalfArithmetic, "exact inputs disabled unrelated half arithmetic");
            requireExactInputs(mixed.bytes, indexedMemory);
        }
        requireFailure([&] {
            static_cast<void>(detail::convertLsfgShaderToFp16({original.data(), original.size() - 4},
                contract, "truncated synthetic"));
        });
        const auto inert = detail::convertLsfgShaderToFp16(shader(contract, false), contract, "inert");
        require(!inert.hasHalfArithmetic, "inert shader was misreported as FP16 arithmetic");

        const auto archive = graph();
        const auto fp32 = loadLsfgShaderSet(archive, "synthetic.dll", false);
        require(!fp32.fp16 && !fp32.translated, "FP16-off changed native FP32 selection");
        const auto fp16 = loadLsfgShaderSet(archive, "synthetic.dll", true);
        require(fp16.fp16 && fp16.translated && fp16.convertedFp16Stages == 46,
            "FP32-only native graph was not forced to FP16");
        for (const bool mode : {false, true})
            for (const auto& spec : detail::lsfgShaderSpecs(mode))
                if (spec.logicalId == 255 || spec.logicalId == 256)
                    require(fp16.resource(archive, spec.logicalId, mode) ==
                            fp32.resource(archive, spec.logicalId, mode),
                        "forced FP16 changed image preparation or final colour reconstruction");
        require(loadLsfgShaderSet(archive, "synthetic.dll", true).translated == fp16.translated,
            "repeated FP16 setup did not reuse its archive cache");
        require(!loadLsfgShaderSet(archive, "synthetic.dll", false).translated,
            "converted cache contaminated an FP16-off choice");
        for (const bool mode : {false, true}) {
            const auto selected = loadLsfgShaderSet(archive, "synthetic.dll", true, mode);
            const auto expected = std::ranges::count_if(detail::lsfgShaderSpecs(mode),
                [](const auto& spec) { return spec.logicalId != 255 && spec.logicalId != 256; });
            require(selected.convertedFp16Stages == static_cast<size_t>(expected),
                "mode-specific preflight converted the wrong graph");
            for (const auto& spec : detail::lsfgShaderSpecs(mode))
                require(detail::validateSpirvComputeShader(
                    selected.resource(archive, spec.logicalId, mode), spec.contract,
                    "converted graph").declaresFloat16 ==
                        (spec.logicalId != 255 && spec.logicalId != 256),
                    "stage did not retain the intended arithmetic precision");
        }
        const auto replacement = graph();
        require(loadLsfgShaderSet(replacement, "synthetic.dll", true).translated != fp16.translated,
            "replacement archive reused stale converted resources");
        const auto inertGraph = graph(false);
        requireFailure([&] { static_cast<void>(loadLsfgShaderSet(inertGraph, "synthetic.dll", true)); });
        requireFailure([&] { static_cast<void>(loadLsfgShaderSet(inertGraph, "synthetic.dll", true)); });
        auto native = graph();
        for (const bool mode : {false, true})
            for (const auto& spec : detail::lsfgShaderSpecs(mode))
                native.resources.emplace(detail::lsfgResourceId(spec.logicalId, true, mode),
                    fp16.resource(archive, spec.logicalId, mode));
        const auto preferred = loadLsfgShaderSet(native, "synthetic.dll", true);
        require(preferred.fp16 && !preferred.translated && !preferred.convertedFp16Stages,
            "native FP16 did not take priority over conversion");
    }
}

int main() {
    try { tests(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
