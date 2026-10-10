/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "spirv_image_format.hpp"

#include "mako-common/helpers/errors.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
    constexpr uint32_t spirvMagic = 0x07230203;
    constexpr uint16_t opCapability = 17;
    constexpr uint16_t opTypeImage = 25;
    constexpr uint32_t capabilityShader = 1;
    constexpr uint32_t capabilityStorageImageExtendedFormats = 49;
    constexpr uint32_t capabilityStorageImageWriteWithoutFormat = 56;
    constexpr uint32_t imageFormatRgb10A2 = 11;
    constexpr uint32_t imageFormatR8 = 15;
    constexpr uint32_t imageFormatR8Snorm = 20;

    [[nodiscard]] uint32_t storageImageCapability(
            const uint32_t imageFormat) {
        return (imageFormat == imageFormatRgb10A2 || imageFormat == imageFormatR8 || imageFormat == imageFormatR8Snorm)
            ? capabilityStorageImageExtendedFormats
            : capabilityShader;
    }
}

void mako::backend::detail::patchStorageImageFormat(
        std::vector<uint8_t>& data, const uint32_t imageFormat) {
    if (data.size() < 5 * sizeof(uint32_t) ||
            data.size() % sizeof(uint32_t) != 0) {
        throw ls::error("translated shader has an invalid SPIR-V size");
    }
    uint32_t headerMagic{};
    std::memcpy(&headerMagic, data.data(), sizeof(headerMagic));
    if (headerMagic != spirvMagic)
        throw ls::error("translated shader has invalid SPIR-V magic");

    const size_t totalWordCount = data.size() / sizeof(uint32_t);
    const auto readWord = [&data](const size_t index) {
        uint32_t value{};
        std::memcpy(
            &value, data.data() + index * sizeof(uint32_t), sizeof(value)
        );
        return value;
    };
    const auto writeWord = [&data](
            const size_t index, const uint32_t value) {
        std::memcpy(
            data.data() + index * sizeof(uint32_t), &value, sizeof(value)
        );
    };
    bool patchedImage = false;
    bool hasRequiredFormatCapability = storageImageCapability(imageFormat) == capabilityShader;
    for (size_t i = 5; i < totalWordCount;) {
        const uint32_t instruction = readWord(i);
        const uint16_t wordCount = static_cast<uint16_t>(instruction >> 16U);
        const uint16_t opcode = static_cast<uint16_t>(instruction & 0xffffU);
        if (wordCount == 0 || i + wordCount > totalWordCount)
            throw ls::error("translated shader contains invalid SPIR-V");
        if (opcode == opCapability && wordCount >= 2) {
            const uint32_t capability = readWord(i + 1);
            if (capability == capabilityStorageImageExtendedFormats)
                hasRequiredFormatCapability = true;
            if (capability == capabilityStorageImageWriteWithoutFormat) {
                const uint32_t replacement = storageImageCapability(imageFormat);
                writeWord(i + 1, replacement);
                if (replacement == capabilityStorageImageExtendedFormats)
                    hasRequiredFormatCapability = true;
            }
        }
        if (opcode == opTypeImage && wordCount >= 9 &&
                readWord(i + 7) == 2) {
            writeWord(i + 8, imageFormat);
            patchedImage = true;
        }
        i += wordCount;
    }
    if (!patchedImage)
        throw ls::error("translated shader has no storage image");
    if (!hasRequiredFormatCapability) {
        throw ls::error(
            "translated shader cannot declare its storage image format"
        );
    }
}
