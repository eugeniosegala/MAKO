/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "extraction/lsfg_shader_set.hpp"
#include "extraction/spirv_fp16.hpp"
#include "mako-common/helpers/errors.hpp"

#include <spirv-tools/libspirv.hpp>

#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

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
            const bool arithmetic = true) {
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
%float = OpTypeFloat 32
%v2u = OpTypeVector %uint 2
%v3u = OpTypeVector %uint 3
%v4f = OpTypeVector %float 4
%input = OpTypePointer Input %v3u
%gid = OpVariable %input Input
%block = OpTypeStruct %float
%pu = OpTypePointer Uniform %block
%sampler = OpTypeSampler
%ps = OpTypePointer UniformConstant %sampler
%sampled = OpTypeImage %float 2D 0 0 0 1 Unknown
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
        require(fp16.fp16 && fp16.translated && fp16.convertedFp16Stages == 48,
            "FP32-only native graph was not forced to FP16");
        require(loadLsfgShaderSet(archive, "synthetic.dll", true).translated == fp16.translated,
            "repeated FP16 setup did not reuse its archive cache");
        require(!loadLsfgShaderSet(archive, "synthetic.dll", false).translated,
            "converted cache contaminated an FP16-off choice");
        for (const bool mode : {false, true}) {
            const auto selected = loadLsfgShaderSet(archive, "synthetic.dll", true, mode);
            require(selected.convertedFp16Stages == detail::lsfgShaderSpecs(mode).size(),
                "mode-specific preflight converted the wrong graph");
            for (const auto& spec : detail::lsfgShaderSpecs(mode))
                require(detail::validateSpirvComputeShader(
                    selected.resource(archive, spec.logicalId, mode), spec.contract,
                    "converted graph").declaresFloat16, "converted stage lost FP16");
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
