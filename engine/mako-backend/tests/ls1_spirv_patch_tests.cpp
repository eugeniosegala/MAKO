/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "extraction/ls1_spirv_patch.hpp"
#include "mako-common/helpers/errors.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    constexpr uint32_t spirvMagic = 0x07230203;
    constexpr uint32_t opCapability = 17;
    constexpr uint32_t opTypeImage = 25;
    constexpr uint32_t capabilityShader = 1;
    constexpr uint32_t capabilityStorageImageExtendedFormats = 49;
    constexpr uint32_t capabilityStorageImageWriteWithoutFormat = 56;
    constexpr uint32_t imageFormatRgba8 = 4;
    constexpr uint32_t imageFormatR8Snorm = 20;

    void require(const bool condition, const std::string& message) {
        if (!condition)
            throw std::runtime_error(message);
    }

    [[nodiscard]] std::vector<uint8_t> shaderWords() {
        const std::vector<uint32_t> words{
            spirvMagic, 0x00010000, 0, 16, 0,
            (2U << 16U) | opCapability, capabilityShader,
            (2U << 16U) | opCapability,
                capabilityStorageImageWriteWithoutFormat,
            (9U << 16U) | opTypeImage,
                1, 2, 2, 0, 0, 0, 2, 0,
        };
        std::vector<uint8_t> bytes(words.size() * sizeof(uint32_t));
        std::memcpy(bytes.data(), words.data(), bytes.size());
        return bytes;
    }

    [[nodiscard]] std::vector<uint8_t> shaderWithoutPatchableCapability() {
        auto bytes = shaderWords();
        const uint32_t capability = capabilityShader;
        std::memcpy(
            bytes.data() + 8 * sizeof(uint32_t),
            &capability,
            sizeof(capability)
        );
        return bytes;
    }

    [[nodiscard]] uint32_t readWord(
            const std::vector<uint8_t>& bytes, const size_t index) {
        uint32_t result{};
        std::memcpy(
            &result, bytes.data() + index * sizeof(uint32_t), sizeof(result)
        );
        return result;
    }

    void testRgba8Patch() {
        auto shader = shaderWords();
        mako::backend::detail::patchLs1StorageImageFormat(
            shader, imageFormatRgba8
        );
        require(readWord(shader, 8) == capabilityShader,
            "Rgba8 retained the write-without-format capability");
        require(readWord(shader, 17) == imageFormatRgba8,
            "Rgba8 did not patch the storage image format");
    }

    void testExtendedFormatPatch() {
        auto shader = shaderWords();
        mako::backend::detail::patchLs1StorageImageFormat(
            shader, imageFormatR8Snorm
        );
        require(readWord(shader, 8) == capabilityStorageImageExtendedFormats,
            "R8Snorm did not declare StorageImageExtendedFormats");
        require(readWord(shader, 17) == imageFormatR8Snorm,
            "R8Snorm did not patch the storage image format");
    }

    void testMissingExtendedFormatCapabilityFailsClosed() {
        auto shader = shaderWithoutPatchableCapability();
        try {
            mako::backend::detail::patchLs1StorageImageFormat(
                shader, imageFormatR8Snorm
            );
            throw std::runtime_error(
                "R8Snorm patch accepted a shader without a capability slot"
            );
        } catch (const ls::error&) {
        }
    }
}

int main() {
    try {
        testRgba8Patch();
        testExtendedFormatPatch();
        testMissingExtendedFormatCapabilityFailsClosed();
    } catch (const std::exception& error) {
        std::cerr << "LS1 SPIR-V patch test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
