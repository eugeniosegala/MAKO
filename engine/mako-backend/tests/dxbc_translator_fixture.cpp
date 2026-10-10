/* SPDX-License-Identifier: GPL-3.0-or-later */

// Synthetic public-ABI fixture. It reflects the supplied descriptor mapping
// into structural SPIR-V, without a compiler, Vulkan, or licensed bytecode.
#include "extraction/dxbc_translation_abi.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

using namespace mako::backend::dxbc_abi;

namespace {
    size_t calls = 0;
    void append(std::vector<uint32_t>& words, const uint32_t op,
            const std::initializer_list<uint32_t> operands) {
        words.push_back((static_cast<uint32_t>(operands.size() + 1) << 16U) | op);
        words.insert(words.end(), operands.begin(), operands.end());
    }
}

#define EXPORTED extern "C" __attribute__((visibility("default")))

EXPORTED size_t mako_test_translation_calls() { return calls; }

EXPORTED int vkd3d_shader_compile(const CompileInfo* info, ShaderCode* output, char**) {
    ++calls;
    const char* failure = std::getenv("MAKO_TEST_TRANSLATION_FAILURE");
    if (failure && std::strcmp(failure, "compile") == 0)
        return -1;
    if (!info || info->type != StructureType::CompileInfo || info->sourceType != SourceType::DxbcTpf ||
            info->targetType != TargetType::SpirvBinary || !info->next ||
            static_cast<const InterfaceInfo*>(info->next)->type != StructureType::InterfaceInfo)
        return -2;
    std::vector<uint32_t> words{0x07230203U, 0x00010000U, 0U, 256U, 0U};
    append(words, 17, {1});
    append(words, 17, {56});
    append(words, 15, {5, 1, 0x6e69616dU, 0});
    append(words, 22, {2, 32});
    if (failure && std::strcmp(failure, "precision") == 0)
        append(words, 22, {7, 16});
    append(words, 26, {3});
    append(words, 25, {4, 2, 2, 0, 0, 0, 1, 0});
    append(words, 25, {5, 2, 2, 0, 0, 0, 2, 0});
    append(words, 30, {6, 2});
    append(words, 32, {10, 0, 3});
    append(words, 32, {11, 0, 4});
    append(words, 32, {12, 0, 5});
    append(words, 32, {13, 2, 6});
    const auto& interface = *static_cast<const InterfaceInfo*>(info->next);
    for (uint32_t i = 0; i < interface.bindingCount; ++i) {
        const auto& binding = interface.bindings[i];
        if (binding.registerSpace != 0 || binding.visibility != Visibility::Compute || binding.binding.count != 1)
            return -3;
        const uint32_t pointer = binding.type == DescriptorType::Srv ? 11U : binding.type == DescriptorType::Uav ? 12U
            : binding.type == DescriptorType::Cbv ? 13U : 10U;
        const uint32_t id = 20U + i;
        append(words, 59, {pointer, id, binding.type == DescriptorType::Cbv ? 2U : 0U});
        append(words, 71, {id, 34, binding.binding.set});
        append(words, 71, {id, 33, binding.binding.binding +
            ((failure && std::strcmp(failure, "binding") == 0) ? 100U : 0U)});
    }
    if (failure && std::strcmp(failure, "malformed") == 0)
        words[0] = 0;
    output->size = words.size() * sizeof(uint32_t);
    void* data = std::malloc(output->size);
    if (!data)
        return -4;
    std::memcpy(data, words.data(), output->size);
    output->code = data;
    return 0;
}

EXPORTED void vkd3d_shader_free_shader_code(ShaderCode* code) {
    std::free(const_cast<void*>(code->code));
    *code = {};
}
EXPORTED void vkd3d_shader_free_messages(char* messages) { std::free(messages); }
