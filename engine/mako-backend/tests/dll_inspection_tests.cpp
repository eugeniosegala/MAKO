/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "extraction/content_hash.hpp"
#include "extraction/dll_reader.hpp"
#include "extraction/model_resource_validation.hpp"
#include "mako-common/helpers/errors.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

namespace {
    void require(const bool condition, const std::string& message) {
        if (!condition)
            throw std::runtime_error(message);
    }

    template<typename Action>
    void requireFailure(Action action, const std::string& message) {
        try {
            action();
            throw std::runtime_error(message);
        } catch (const ls::error&) {
        }
    }

    template<typename Integer>
    void writeInteger(std::vector<uint8_t>& data, const size_t offset,
            const Integer value) {
        if (offset > data.size() || sizeof(value) > data.size() - offset)
            throw std::runtime_error("synthetic fixture write exceeded its buffer");
        std::memcpy(data.data() + offset, &value, sizeof(value));
    }

    struct SyntheticPeOptions {
        bool pe32{false};
        bool extraRootResource{false};
        bool namedRootResource{false};
        bool additionalLanguage{false};
    };

    [[nodiscard]] std::vector<uint8_t> syntheticPe(
            const std::vector<uint8_t>& payload,
            const SyntheticPeOptions options = {}) {
        if (payload.size() > 0x300U)
            throw std::runtime_error("synthetic PE payload is too large");
        std::vector<uint8_t> data(0x600U, 0U);
        constexpr size_t pe = 0x80U;
        constexpr size_t optional = pe + 24U;
        constexpr size_t resources = 0x200U;
        const uint16_t optionalSize = options.pe32 ? 0xe0U : 0xf0U;
        const size_t section = optional + optionalSize;
        const size_t numberOfDirectoriesOffset = options.pe32 ? 92U : 108U;
        const size_t resourceDirectoryOffset = options.pe32 ? 112U : 128U;
        writeInteger<uint16_t>(data, 0U, 0x5a4dU);
        writeInteger<int32_t>(data, 60U, static_cast<int32_t>(pe));
        writeInteger<uint32_t>(data, pe, 0x00004550U);
        writeInteger<uint16_t>(data, pe + 6U, 1U);
        writeInteger<uint16_t>(data, pe + 20U, optionalSize);
        writeInteger<uint16_t>(
            data, optional, options.pe32 ? 0x010bU : 0x020bU
        );
        writeInteger<uint32_t>(data, optional + numberOfDirectoriesOffset, 16U);
        writeInteger<uint32_t>(
            data, optional + resourceDirectoryOffset, 0x1000U
        );
        writeInteger<uint32_t>(
            data, optional + resourceDirectoryOffset + 4U, 0x400U
        );
        writeInteger<uint32_t>(data, section + 8U, 0x400U);
        writeInteger<uint32_t>(data, section + 12U, 0x1000U);
        writeInteger<uint32_t>(data, section + 16U, 0x400U);
        writeInteger<uint32_t>(data, section + 20U, 0x200U);

        writeInteger<uint16_t>(
            data, resources + 12U, options.namedRootResource ? 1U : 0U
        );
        writeInteger<uint16_t>(data, resources + 14U,
            options.extraRootResource ? 2U : 1U);
        size_t rootEntry = resources + 16U;
        if (options.namedRootResource) {
            writeInteger<uint32_t>(data, rootEntry, 0x80000380U);
            writeInteger<uint32_t>(data, rootEntry + 4U, 0U);
            rootEntry += 8U;
        }
        if (options.extraRootResource) {
            writeInteger<uint32_t>(data, rootEntry, 5U);
            writeInteger<uint32_t>(data, rootEntry + 4U, 0U);
            rootEntry += 8U;
        }
        writeInteger<uint32_t>(data, rootEntry, 10U);
        writeInteger<uint32_t>(data, rootEntry + 4U, 0x80000040U);
        writeInteger<uint16_t>(data, resources + 0x40U + 14U, 1U);
        writeInteger<uint32_t>(data, resources + 0x50U, 141U);
        writeInteger<uint32_t>(data, resources + 0x54U, 0x80000060U);
        writeInteger<uint16_t>(
            data, resources + 0x60U + 14U,
            options.additionalLanguage ? 2U : 1U
        );
        writeInteger<uint32_t>(data, resources + 0x70U, 0U);
        writeInteger<uint32_t>(data, resources + 0x74U, 0x90U);
        if (options.additionalLanguage) {
            writeInteger<uint32_t>(data, resources + 0x78U, 1033U);
            writeInteger<uint32_t>(data, resources + 0x7cU, 0xa0U);
        }
        writeInteger<uint32_t>(data, resources + 0x90U, 0x1100U);
        writeInteger<uint32_t>(
            data, resources + 0x94U, static_cast<uint32_t>(payload.size())
        );
        if (options.additionalLanguage) {
            writeInteger<uint32_t>(data, resources + 0xa0U, 0x1100U);
            writeInteger<uint32_t>(
                data, resources + 0xa4U, static_cast<uint32_t>(payload.size())
            );
        }
        std::copy(payload.begin(), payload.end(), data.begin() + 0x300U);
        return data;
    }

    void writeFile(const std::filesystem::path& path,
            const std::vector<uint8_t>& data) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size())
        );
        if (!output)
            throw std::runtime_error("failed to write synthetic DLL fixture");
    }

    class TemporaryDirectory {
    public:
        TemporaryDirectory() : path(
                std::filesystem::temp_directory_path() /
                ("mako-dll-inspection-tests-" + std::to_string(::getpid()))) {
            std::filesystem::create_directories(this->path);
        }
        ~TemporaryDirectory() {
            std::error_code ignored;
            std::filesystem::remove_all(this->path, ignored);
        }
        std::filesystem::path path;
    };

    [[nodiscard]] uint32_t instruction(
            const uint16_t opcode, const size_t operandCount) {
        return (static_cast<uint32_t>(operandCount + 1U) << 16U) | opcode;
    }

    void appendInstruction(std::vector<uint32_t>& words,
            const uint16_t opcode,
            const std::initializer_list<uint32_t> operands) {
        words.push_back(instruction(opcode, operands.size()));
        words.insert(words.end(), operands.begin(), operands.end());
    }

    [[nodiscard]] std::vector<uint8_t> syntheticSpirv(
            const mako::backend::detail::ShaderResourceContract& contract,
            const bool includeExtraSampler = false) {
        std::vector<uint32_t> words{
            0x07230203U, 0x00010000U, 0U, 256U, 0U,
        };
        appendInstruction(words, 17U, {1U});
        appendInstruction(words, 15U, {5U, 1U, 0x6e69616dU, 0U});
        appendInstruction(words, 22U, {2U, 32U});
        appendInstruction(words, 26U, {3U});
        appendInstruction(words, 25U, {4U, 2U, 2U, 0U, 0U, 0U, 1U, 0U});
        appendInstruction(words, 25U, {5U, 2U, 2U, 0U, 0U, 0U, 2U, 4U});
        appendInstruction(words, 30U, {6U, 2U});
        appendInstruction(words, 32U, {10U, 0U, 3U});
        appendInstruction(words, 32U, {11U, 0U, 4U});
        appendInstruction(words, 32U, {12U, 0U, 5U});
        appendInstruction(words, 32U, {13U, 2U, 6U});

        uint32_t variable = 20U;
        const auto addDescriptor = [&words, &variable](
                const uint32_t pointer, const uint32_t storage,
                const uint32_t binding) {
            const uint32_t id = variable++;
            appendInstruction(words, 59U, {pointer, id, storage});
            appendInstruction(words, 71U, {id, 34U, 0U});
            appendInstruction(words, 71U, {id, 33U, binding});
        };
        for (size_t index = 0; index < contract.uniformBuffers; ++index)
            addDescriptor(13U, 2U, static_cast<uint32_t>(index));
        for (size_t index = 0; index < contract.samplers; ++index)
            addDescriptor(10U, 0U, static_cast<uint32_t>(16U + index));
        for (size_t index = 0; index < contract.sampledImages; ++index)
            addDescriptor(11U, 0U, static_cast<uint32_t>(32U + index));
        for (size_t index = 0; index < contract.storageImages; ++index)
            addDescriptor(12U, 0U, static_cast<uint32_t>(48U + index));
        if (includeExtraSampler)
            addDescriptor(10U, 0U, 31U);

        std::vector<uint8_t> result(words.size() * sizeof(uint32_t));
        std::memcpy(result.data(), words.data(), result.size());
        return result;
    }

    [[nodiscard]] std::vector<uint8_t> syntheticDxbc(
            const bool shaderChunk = true) {
        std::vector<uint8_t> data(52U, 0U);
        writeInteger<uint32_t>(data, 0U, 0x43425844U);
        writeInteger<uint32_t>(data, 24U, 44U);
        writeInteger<uint32_t>(data, 28U, 1U);
        writeInteger<uint32_t>(data, 32U, 36U);
        writeInteger<uint32_t>(
            data, 36U, shaderChunk ? 0x58454853U : 0x46454452U
        );
        writeInteger<uint32_t>(data, 40U, 0U);
        return data; // Eight trailing vendor bytes are deliberately accepted.
    }

    void testSha256() {
        const std::array<uint8_t, 3> input{'a', 'b', 'c'};
        require(mako::backend::detail::sha256Hex(input) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 implementation returned the wrong known digest");
    }

    void testPeExtractionAndIdentity(const TemporaryDirectory& temporary) {
        const auto firstPath = temporary.path / "first.dll";
        writeFile(firstPath, syntheticPe(
            {1U, 2U, 3U}, {
                .extraRootResource = true,
                .namedRootResource = true,
                .additionalLanguage = true,
            }
        ));
        const auto first = mako::backend::loadDllResourceArchive(firstPath);
        require(first->resources.size() == 1U &&
                first->resources.at(141U) == std::vector<uint8_t>({1U, 2U, 3U}),
            "valid synthetic PE resource was not extracted");
        require(first->fileSha256.size() == 64U &&
                first->resourceLayoutSha256.size() == 64U,
            "DLL fingerprints were not emitted");

        const auto oldTime = std::filesystem::last_write_time(firstPath);
        writeFile(firstPath, syntheticPe(
            {3U, 2U, 1U}, {
                .extraRootResource = true,
                .namedRootResource = true,
                .additionalLanguage = true,
            }
        ));
        std::filesystem::last_write_time(firstPath, oldTime);
        const auto replaced = mako::backend::loadDllResourceArchive(firstPath);
        require(first->fileSha256 != replaced->fileSha256 &&
                first->resourceLayoutSha256 != replaced->resourceLayoutSha256,
            "same-size, restored-mtime DLL replacement reused stale resources");

        const auto pe32Path = temporary.path / "pe32.dll";
        writeFile(pe32Path, syntheticPe(
            {4U, 5U, 6U}, {.pe32 = true, .additionalLanguage = true}
        ));
        const auto pe32 = mako::backend::loadDllResourceArchive(pe32Path);
        require(pe32->resources.at(141U) ==
                std::vector<uint8_t>({4U, 5U, 6U}),
            "valid PE32 resource was not extracted");

        auto negativeOffset = syntheticPe({1U});
        writeInteger<int32_t>(negativeOffset, 60U, -1);
        const auto negativePath = temporary.path / "negative.dll";
        writeFile(negativePath, negativeOffset);
        requireFailure(
            [&] { static_cast<void>(mako::backend::loadDllResourceArchive(
                negativePath)); },
            "negative PE offset was accepted"
        );

        auto outside = syntheticPe({1U});
        writeInteger<uint32_t>(outside, 0x290U, 0x2000U);
        const auto outsidePath = temporary.path / "outside.dll";
        writeFile(outsidePath, outside);
        requireFailure(
            [&] { static_cast<void>(mako::backend::loadDllResourceArchive(
                outsidePath)); },
            "out-of-range PE resource was accepted"
        );
    }

    void testShaderContainersAndRequiredBindings() {
        const mako::backend::detail::ShaderResourceContract contract{1, 1, 1, 1};
        auto compatible = syntheticSpirv(contract, true);
        mako::backend::detail::validateSpirvComputeShader(
            compatible, contract, "synthetic SPIR-V"
        );
        auto missing = syntheticSpirv({0, 1, 1, 1});
        requireFailure(
            [&] { mako::backend::detail::validateSpirvComputeShader(
                missing, contract, "missing binding"); },
            "missing required SPIR-V binding was accepted"
        );
        compatible[0] = 0U;
        requireFailure(
            [&] { mako::backend::detail::validateSpirvComputeShader(
                compatible, contract, "bad magic"); },
            "invalid SPIR-V magic was accepted"
        );

        mako::backend::detail::validateDxbcComputeShader(
            syntheticDxbc(), "synthetic DXBC"
        );
        requireFailure(
            [&] { mako::backend::detail::validateDxbcComputeShader(
                syntheticDxbc(false), "missing shader chunk"); },
            "DXBC without shader bytecode was accepted"
        );
    }

    void testCapabilityIsolationAndHarmlessAdditions() {
        for (const bool performance : {false, true}) {
            for (const bool fp16 : {false, true}) {
                std::unordered_map<uint32_t, std::vector<uint8_t>> resources;
                for (const auto& spec :
                        mako::backend::detail::lsfgShaderSpecs(performance)) {
                    resources.emplace(
                        mako::backend::detail::lsfgResourceId(
                            spec.logicalId, fp16, performance
                        ),
                        syntheticSpirv(spec.contract, true)
                    );
                }
                resources.emplace(9999U, std::vector<uint8_t>{1U, 2U, 3U});
                mako::backend::detail::validateLsfgModelResources(
                    resources, fp16, performance
                );
                resources.erase(mako::backend::detail::lsfgResourceId(
                    mako::backend::detail::lsfgShaderSpecs(performance).front().logicalId,
                    fp16, performance
                ));
                requireFailure(
                    [&] { mako::backend::detail::validateLsfgModelResources(
                        resources, fp16, performance); },
                    "missing LSFG resource was accepted"
                );
            }
        }

        std::unordered_map<uint32_t, std::vector<uint8_t>> ls1Resources;
        for (uint32_t id = 141U; id <= 161U; ++id)
            ls1Resources.emplace(id, syntheticDxbc());
        ls1Resources.emplace(9999U, std::vector<uint8_t>{1U});
        mako::backend::detail::validateLs1ModelResources(
            ls1Resources, mako::backend::Ls1Mode::Quality
        );
        mako::backend::detail::validateLs1ModelResources(
            ls1Resources, mako::backend::Ls1Mode::Performance
        );
        ls1Resources.erase(141U);
        mako::backend::detail::validateLs1ModelResources(
            ls1Resources, mako::backend::Ls1Mode::Quality
        );
        requireFailure(
            [&] { mako::backend::detail::validateLs1ModelResources(
                ls1Resources, mako::backend::Ls1Mode::Performance); },
            "LS1 Performance did not fail independently of Quality"
        );
    }
}

int main() {
    try {
        const TemporaryDirectory temporary;
        testSha256();
        testPeExtractionAndIdentity(temporary);
        testShaderContainersAndRequiredBindings();
        testCapabilityIsolationAndHarmlessAdditions();
    } catch (const std::exception& error) {
        std::cerr << "DLL inspection test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
