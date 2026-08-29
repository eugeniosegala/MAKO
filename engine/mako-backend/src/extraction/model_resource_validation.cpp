/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "model_resource_validation.hpp"

#include "mako-common/helpers/errors.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
    constexpr uint32_t spirvMagic = 0x07230203U;
    constexpr uint16_t opCapability = 17U;
    constexpr uint16_t opEntryPoint = 15U;
    constexpr uint16_t opTypeImage = 25U;
    constexpr uint16_t opTypeSampler = 26U;
    constexpr uint16_t opTypeSampledImage = 27U;
    constexpr uint16_t opTypePointer = 32U;
    constexpr uint16_t opVariable = 59U;
    constexpr uint16_t opDecorate = 71U;
    constexpr uint32_t capabilityShader = 1U;
    constexpr uint32_t executionModelCompute = 5U;
    constexpr uint32_t storageUniformConstant = 0U;
    constexpr uint32_t storageUniform = 2U;
    constexpr uint32_t storageStorageBuffer = 12U;
    constexpr uint32_t decorationBinding = 33U;
    constexpr uint32_t decorationDescriptorSet = 34U;
    constexpr uint32_t fourccDxbc = 0x43425844U;
    constexpr uint32_t fourccShdr = 0x52444853U;
    constexpr uint32_t fourccShex = 0x58454853U;

    enum class DescriptorKind : uint8_t {
        SampledImage,
        StorageImage,
        UniformBuffer,
        Sampler,
        Unsupported,
    };

    struct Type {
        enum class Kind : uint8_t {
            Image,
            Sampler,
            SampledImage,
            Pointer,
            Other,
        } kind{Kind::Other};
        uint32_t storageClass{0};
        uint32_t pointee{0};
        uint32_t sampled{0};
    };

    struct Variable {
        uint32_t pointerType{0};
        uint32_t storageClass{0};
    };

    struct Decorations {
        std::optional<uint32_t> descriptorSet;
        std::optional<uint32_t> binding;
    };

    [[nodiscard]] uint32_t readWord(
            const std::span<const uint8_t> data, const size_t index) {
        uint32_t value{};
        std::memcpy(
            &value, data.data() + index * sizeof(uint32_t), sizeof(value)
        );
        return value;
    }

    [[nodiscard]] uint32_t readLittleEndian(
            const std::span<const uint8_t> data, const size_t offset,
            const std::string& sourceName) {
        if (offset > data.size() || sizeof(uint32_t) > data.size() - offset)
            throw ls::error(sourceName + " contains a truncated integer");
        uint32_t value{};
        std::memcpy(&value, data.data() + offset, sizeof(value));
        return value;
    }

    [[nodiscard]] std::string readSpirvString(
            const std::span<const uint8_t> data, const size_t firstWord,
            const size_t endWord, const std::string& sourceName) {
        std::string result;
        for (size_t word = firstWord; word < endWord; ++word) {
            const uint32_t value = readWord(data, word);
            for (uint32_t shift = 0; shift < 32U; shift += 8U) {
                const char character = static_cast<char>(value >> shift);
                if (character == '\0')
                    return result;
                result.push_back(character);
            }
        }
        throw ls::error(sourceName + " contains an unterminated SPIR-V string");
    }

    [[nodiscard]] DescriptorKind descriptorKind(
            const Variable& variable,
            const std::unordered_map<uint32_t, Type>& types) {
        const auto pointer = types.find(variable.pointerType);
        if (pointer == types.end() || pointer->second.kind != Type::Kind::Pointer)
            return DescriptorKind::Unsupported;
        if (variable.storageClass == storageUniform)
            return DescriptorKind::UniformBuffer;
        if (variable.storageClass == storageStorageBuffer)
            return DescriptorKind::Unsupported;
        const auto pointee = types.find(pointer->second.pointee);
        if (pointee == types.end())
            return DescriptorKind::Unsupported;
        if (variable.storageClass != storageUniformConstant)
            return DescriptorKind::Unsupported;
        if (pointee->second.kind == Type::Kind::Sampler)
            return DescriptorKind::Sampler;
        if (pointee->second.kind == Type::Kind::Image) {
            if (pointee->second.sampled == 1U)
                return DescriptorKind::SampledImage;
            if (pointee->second.sampled == 2U)
                return DescriptorKind::StorageImage;
        }
        return DescriptorKind::Unsupported;
    }

    [[nodiscard]] std::map<std::pair<uint32_t, uint32_t>, DescriptorKind>
    requiredBindings(
            const mako::backend::detail::ShaderResourceContract& contract) {
        std::map<std::pair<uint32_t, uint32_t>, DescriptorKind> result;
        for (size_t index = 0; index < contract.uniformBuffers; ++index)
            result.emplace(std::pair{0U, static_cast<uint32_t>(index)},
                DescriptorKind::UniformBuffer);
        for (size_t index = 0; index < contract.samplers; ++index)
            result.emplace(std::pair{0U, static_cast<uint32_t>(16U + index)},
                DescriptorKind::Sampler);
        for (size_t index = 0; index < contract.sampledImages; ++index)
            result.emplace(std::pair{0U, static_cast<uint32_t>(32U + index)},
                DescriptorKind::SampledImage);
        for (size_t index = 0; index < contract.storageImages; ++index)
            result.emplace(std::pair{0U, static_cast<uint32_t>(48U + index)},
                DescriptorKind::StorageImage);
        return result;
    }

    constexpr std::array qualitySpecs{
        mako::backend::detail::LsfgShaderSpec{255, {1, 7, 1, 1}},
        mako::backend::detail::LsfgShaderSpec{256, {5, 1, 1, 2}},
        mako::backend::detail::LsfgShaderSpec{267, {1, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{268, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{269, {2, 4, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{270, {4, 4, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{275, {12, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{276, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{277, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{278, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{279, {2, 6, 1, 1}},
        mako::backend::detail::LsfgShaderSpec{257, {9, 3, 1, 2}},
        mako::backend::detail::LsfgShaderSpec{259, {3, 4, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{260, {4, 4, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{261, {4, 4, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{262, {6, 1, 1, 2}},
        mako::backend::detail::LsfgShaderSpec{263, {3, 4, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{264, {4, 4, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{265, {4, 4, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{266, {6, 1, 1, 2}},
        mako::backend::detail::LsfgShaderSpec{258, {10, 2, 1, 2}},
        mako::backend::detail::LsfgShaderSpec{271, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{272, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{273, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{274, {3, 1, 1, 2}},
    };

    constexpr std::array performanceSpecs{
        mako::backend::detail::LsfgShaderSpec{267, {1, 1, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{268, {1, 1, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{269, {1, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{270, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{275, {6, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{276, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{277, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{278, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{279, {2, 6, 1, 1}},
        mako::backend::detail::LsfgShaderSpec{257, {5, 3, 1, 2}},
        mako::backend::detail::LsfgShaderSpec{259, {3, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{260, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{261, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{262, {4, 1, 1, 2}},
        mako::backend::detail::LsfgShaderSpec{263, {3, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{264, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{265, {2, 2, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{266, {4, 1, 1, 2}},
        mako::backend::detail::LsfgShaderSpec{258, {6, 1, 1, 2}},
        mako::backend::detail::LsfgShaderSpec{271, {1, 1, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{272, {1, 1, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{273, {1, 1, 0, 1}},
        mako::backend::detail::LsfgShaderSpec{274, {2, 1, 1, 2}},
    };
}

void mako::backend::detail::validateDxbcComputeShader(
        const std::span<const uint8_t> data,
        const std::string& sourceName) {
    if (data.size() < 32U || readLittleEndian(data, 0, sourceName) != fourccDxbc)
        throw ls::error(sourceName + " is not a DXBC container");
    const uint32_t declaredSize = readLittleEndian(data, 24U, sourceName);
    const uint32_t chunkCount = readLittleEndian(data, 28U, sourceName);
    if (declaredSize < 32U || declaredSize > data.size())
        throw ls::error(sourceName + " has an invalid DXBC container size");
    if (chunkCount == 0U || chunkCount > (declaredSize - 32U) / 4U)
        throw ls::error(sourceName + " has an invalid DXBC chunk count");

    const size_t headerEnd = 32U + static_cast<size_t>(chunkCount) * 4U;
    std::vector<std::pair<size_t, size_t>> ranges;
    ranges.reserve(chunkCount);
    bool hasComputeBytecode = false;
    for (uint32_t index = 0; index < chunkCount; ++index) {
        const uint32_t offset = readLittleEndian(
            data, 32U + static_cast<size_t>(index) * 4U, sourceName
        );
        if (offset < headerEnd || offset > declaredSize ||
                8U > declaredSize - offset) {
            throw ls::error(sourceName + " has an invalid DXBC chunk offset");
        }
        const uint32_t type = readLittleEndian(data, offset, sourceName);
        const uint32_t size = readLittleEndian(data, offset + 4U, sourceName);
        if (size > declaredSize - offset - 8U)
            throw ls::error(sourceName + " has a truncated DXBC chunk");
        ranges.emplace_back(offset, static_cast<size_t>(offset) + 8U + size);
        hasComputeBytecode = hasComputeBytecode ||
            type == fourccShdr || type == fourccShex;
    }
    std::ranges::sort(ranges);
    for (size_t index = 1; index < ranges.size(); ++index) {
        if (ranges[index].first < ranges[index - 1U].second)
            throw ls::error(sourceName + " has overlapping DXBC chunks");
    }
    if (!hasComputeBytecode)
        throw ls::error(sourceName + " has no DXBC shader bytecode chunk");
}

void mako::backend::detail::validateSpirvComputeShader(
        const std::span<const uint8_t> data,
        const ShaderResourceContract& required,
        const std::string& sourceName) {
    if (data.size() < 5U * sizeof(uint32_t) ||
            data.size() % sizeof(uint32_t) != 0U) {
        throw ls::error(sourceName + " has an invalid SPIR-V size");
    }
    if (readWord(data, 0) != spirvMagic)
        throw ls::error(sourceName + " has invalid SPIR-V magic");
    if (readWord(data, 3) == 0U || readWord(data, 4) != 0U)
        throw ls::error(sourceName + " has an invalid SPIR-V header");

    std::unordered_map<uint32_t, Type> types;
    std::unordered_map<uint32_t, Variable> variables;
    std::unordered_map<uint32_t, Decorations> decorations;
    bool hasShaderCapability = false;
    bool hasComputeMain = false;
    const size_t totalWords = data.size() / sizeof(uint32_t);
    for (size_t offset = 5U; offset < totalWords;) {
        const uint32_t instruction = readWord(data, offset);
        const uint16_t count = static_cast<uint16_t>(instruction >> 16U);
        const uint16_t opcode = static_cast<uint16_t>(instruction & 0xffffU);
        if (count == 0U || count > totalWords - offset)
            throw ls::error(sourceName + " contains malformed SPIR-V");
        if (opcode == opCapability && count >= 2U &&
                readWord(data, offset + 1U) == capabilityShader) {
            hasShaderCapability = true;
        } else if (opcode == opEntryPoint && count >= 4U &&
                readWord(data, offset + 1U) == executionModelCompute &&
                readSpirvString(data, offset + 3U, offset + count, sourceName) ==
                    "main") {
            hasComputeMain = true;
        } else if (opcode == opTypeImage && count >= 9U) {
            types.insert_or_assign(readWord(data, offset + 1U), Type{
                .kind = Type::Kind::Image,
                .sampled = readWord(data, offset + 7U),
            });
        } else if (opcode == opTypeSampler && count >= 2U) {
            types.insert_or_assign(
                readWord(data, offset + 1U), Type{.kind = Type::Kind::Sampler}
            );
        } else if (opcode == opTypeSampledImage && count >= 3U) {
            types.insert_or_assign(readWord(data, offset + 1U), Type{
                .kind = Type::Kind::SampledImage,
                .pointee = readWord(data, offset + 2U),
            });
        } else if (opcode == opTypePointer && count >= 4U) {
            types.insert_or_assign(readWord(data, offset + 1U), Type{
                .kind = Type::Kind::Pointer,
                .storageClass = readWord(data, offset + 2U),
                .pointee = readWord(data, offset + 3U),
            });
        } else if (opcode == opVariable && count >= 4U) {
            variables.insert_or_assign(readWord(data, offset + 2U), Variable{
                .pointerType = readWord(data, offset + 1U),
                .storageClass = readWord(data, offset + 3U),
            });
        } else if (opcode == opDecorate && count >= 4U) {
            Decorations& target = decorations[readWord(data, offset + 1U)];
            const uint32_t decoration = readWord(data, offset + 2U);
            if (decoration == decorationBinding)
                target.binding = readWord(data, offset + 3U);
            else if (decoration == decorationDescriptorSet)
                target.descriptorSet = readWord(data, offset + 3U);
        }
        offset += count;
    }
    if (!hasShaderCapability || !hasComputeMain)
        throw ls::error(sourceName + " is not a SPIR-V compute shader with main");

    std::map<std::pair<uint32_t, uint32_t>, DescriptorKind> actual;
    for (const auto& [id, variable] : variables) {
        const auto decorated = decorations.find(id);
        if (decorated == decorations.end() ||
                !decorated->second.descriptorSet || !decorated->second.binding) {
            continue;
        }
        const auto key = std::pair{
            *decorated->second.descriptorSet, *decorated->second.binding
        };
        if (!actual.emplace(key, descriptorKind(variable, types)).second)
            throw ls::error(sourceName + " declares a duplicate descriptor binding");
    }
    for (const auto& [binding, kind] : requiredBindings(required)) {
        const auto found = actual.find(binding);
        if (found == actual.end())
            throw ls::error(sourceName + " is missing a required descriptor binding");
        if (found->second != kind)
            throw ls::error(sourceName + " has an incompatible descriptor binding");
    }
}

uint32_t mako::backend::detail::lsfgResourceId(
        const uint32_t logicalId, const bool fp16,
        const bool performance) {
    constexpr uint32_t baseOffset = 49U;
    constexpr uint32_t performanceOffset = 23U;
    constexpr uint32_t fp32Offset = 49U;
    return baseOffset + logicalId +
        (performance ? performanceOffset : 0U) +
        (fp16 ? 0U : fp32Offset);
}

std::span<const mako::backend::detail::LsfgShaderSpec>
mako::backend::detail::lsfgShaderSpecs(const bool performance) {
    return performance
        ? std::span<const LsfgShaderSpec>{performanceSpecs}
        : std::span<const LsfgShaderSpec>{qualitySpecs};
}

const std::vector<uint8_t>& mako::backend::detail::validatedLsfgResource(
        const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources,
        const uint32_t logicalId, const bool fp16, const bool performance) {
    const auto specs = lsfgShaderSpecs(performance);
    const auto spec = std::ranges::find(
        specs, logicalId, &LsfgShaderSpec::logicalId
    );
    if (spec == specs.end())
        throw ls::error("unknown LSFG shader contract: " + std::to_string(logicalId));
    const uint32_t resourceId = lsfgResourceId(
        logicalId, fp16, performance
    );
    const auto found = resources.find(resourceId);
    if (found == resources.end())
        throw ls::error("Lossless.dll does not contain LSFG resource " +
            std::to_string(resourceId));
    validateSpirvComputeShader(
        found->second, spec->contract, std::to_string(resourceId)
    );
    return found->second;
}

void mako::backend::detail::validateLsfgModelResources(
        const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources,
        const bool fp16, const bool performance) {
    for (const LsfgShaderSpec& spec : lsfgShaderSpecs(performance))
        static_cast<void>(validatedLsfgResource(
            resources, spec.logicalId, fp16, performance
        ));
}

void mako::backend::detail::validateLs1ModelResources(
        const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources,
        const Ls1Mode mode) {
    const auto validate = [&resources](const uint32_t id) {
        const auto found = resources.find(id);
        if (found == resources.end())
            throw ls::error("Lossless.dll does not contain LS1 resource " +
                std::to_string(id));
        validateDxbcComputeShader(found->second, std::to_string(id));
    };
    validate(146U);
    for (uint32_t variant = 0; variant < 5U; ++variant) {
        if (mode == Ls1Mode::Performance) {
            validate(141U + variant);
        } else {
            const uint32_t first = 147U + variant * 3U;
            validate(first);
            validate(first + 1U);
            validate(first + 2U);
        }
    }
}
