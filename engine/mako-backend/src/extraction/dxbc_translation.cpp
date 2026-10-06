/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "dxbc_translation.hpp"
#include "spirv_image_format.hpp"
#include "mako-common/helpers/errors.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace mako;

namespace {
    // Minimal declarations for vkd3d-shader's stable public C ABI. MAKO loads
    // the LGPL vkd3d-shader implementation dynamically and does not incorporate
    // it into the Renderer. Keep these layouts aligned with vkd3d_shader.h.
    enum class StructureType : int32_t {
        CompileInfo = 0,
        InterfaceInfo = 1,
        SpirvTargetInfo = 4,
    };
    enum class SourceType : int32_t {
        DxbcTpf = 1,
    };
    enum class TargetType : int32_t {
        SpirvBinary = 1,
    };
    enum class LogLevel : int32_t {
        Warning = 2,
    };
    enum class DescriptorType : int32_t {
        Srv = 0,
        Uav = 1,
        Cbv = 2,
        Sampler = 3,
    };
    enum class Visibility : int32_t {
        Compute = 1000000000,
    };
    enum class SpirvEnvironment : int32_t {
        Vulkan10 = 2,
    };

    struct ShaderCode {
        const void* code;
        size_t size;
    };
    struct DescriptorBinding {
        uint32_t set;
        uint32_t binding;
        uint32_t count;
    };
    struct ResourceBinding {
        DescriptorType type;
        uint32_t registerSpace;
        uint32_t registerIndex;
        Visibility visibility;
        uint32_t flags;
        DescriptorBinding binding;
    };
    struct InterfaceInfo {
        StructureType type;
        const void* next;
        const ResourceBinding* bindings;
        uint32_t bindingCount;
        const void* pushConstantBuffers;
        uint32_t pushConstantBufferCount;
        const void* combinedSamplers;
        uint32_t combinedSamplerCount;
        const void* uavCounters;
        uint32_t uavCounterCount;
    };
    struct SpirvTargetInfo {
        StructureType type;
        const void* next;
        const char* entryPoint;
        SpirvEnvironment environment;
        const void* extensions;
        uint32_t extensionCount;
        const void* parameters;
        uint32_t parameterCount;
        bool dualSourceBlending;
        const uint32_t* outputSwizzles;
        uint32_t outputSwizzleCount;
    };
    struct CompileInfo {
        StructureType type;
        const void* next;
        ShaderCode source;
        SourceType sourceType;
        TargetType targetType;
        const void* options;
        uint32_t optionCount;
        LogLevel logLevel;
        const char* sourceName;
    };

    using CompileFunction = int (*)(
        const CompileInfo*, ShaderCode*, char**
    );
    using FreeCodeFunction = void (*)(ShaderCode*);
    using FreeMessagesFunction = void (*)(char*);

    constexpr uint32_t bindingFlagImage = 0x2;
    constexpr uint32_t bindingFlagBuffer = 0x1;

    struct LibraryCloser {
        void operator()(void* library) const noexcept {
            if (library)
                dlclose(library);
        }
    };
    using Library = std::unique_ptr<void, LibraryCloser>;

    struct Translator {
        Library library;
        CompileFunction compile{};
        FreeCodeFunction freeCode{};
        FreeMessagesFunction freeMessages{};
        std::string path;
    };

    struct ShaderCodeOwner {
        ShaderCode code{};
        FreeCodeFunction freeCode{};

        ~ShaderCodeOwner() {
            if (code.code && freeCode)
                freeCode(&code);
        }
    };

    struct MessageOwner {
        char* data{};
        FreeMessagesFunction freeMessages{};

        ~MessageOwner() {
            if (data && freeMessages)
                freeMessages(data);
        }
    };

    std::optional<std::filesystem::path> newestLibraryIn(
            const std::filesystem::path& directory) {
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error))
            return std::nullopt;

        std::optional<std::filesystem::path> selected;
        for (const auto& entry : std::filesystem::directory_iterator(
                directory, std::filesystem::directory_options::skip_permission_denied,
                error)) {
            if (error)
                break;
            if (!entry.is_regular_file(error) || error)
                continue;
            const auto filename = entry.path().filename().string();
            if (!filename.starts_with("libvkd3d-shader.so."))
                continue;
            if (!selected || filename > selected->filename().string())
                selected = entry.path();
        }
        return selected;
    }

    void appendSteamRuntimeCandidates(
            std::vector<std::filesystem::path>& candidates,
            const std::filesystem::path& shaderDllPath) {
        const auto common = shaderDllPath.parent_path().parent_path();
        constexpr std::array runtimeNames{
            "SteamLinuxRuntime_4",
            "SteamLinuxRuntime_sniper",
            "SteamLinuxRuntime_soldier",
            "SteamLinuxRuntime",
        };
#if INTPTR_MAX == INT64_MAX
        constexpr std::string_view architecture = "x86_64-linux-gnu";
#else
        constexpr std::string_view architecture = "i386-linux-gnu";
#endif

        std::error_code error;
        for (const auto* runtimeName : runtimeNames) {
            const auto runtime = common / runtimeName;
            if (!std::filesystem::is_directory(runtime, error)) {
                error.clear();
                continue;
            }
            std::vector<std::pair<bool, std::filesystem::path>> runtimeLibraries;
            for (const auto& entry : std::filesystem::directory_iterator(
                    runtime,
                    std::filesystem::directory_options::skip_permission_denied,
                    error)) {
                if (error)
                    break;
                if (!entry.is_directory(error) || error)
                    continue;
                const auto direct = entry.path() / "files/lib" / architecture;
                if (const auto library = newestLibraryIn(direct))
                    runtimeLibraries.emplace_back(true, *library);
                const auto temporary = entry.path() / "usr/lib" / architecture;
                if (const auto library = newestLibraryIn(temporary))
                    runtimeLibraries.emplace_back(false, *library);
            }
            std::ranges::sort(runtimeLibraries,
                [](const auto& left, const auto& right) {
                    if (left.first != right.first)
                        return left.first > right.first;
                    return left.second.string() > right.second.string();
                }
            );
            for (const auto& [stablePlatform, library] : runtimeLibraries) {
                static_cast<void>(stablePlatform);
                candidates.push_back(library);
            }
            error.clear();
        }
    }

    template<typename Function>
    Function loadSymbol(void* library, const char* name) {
        dlerror();
        void* symbol = dlsym(library, name);
        if (const char* error = dlerror())
            throw ls::error(std::string("missing vkd3d-shader symbol ") +
                name + ": " + error);
        return reinterpret_cast<Function>(symbol);
    }

    Translator loadTranslator(const std::filesystem::path& shaderDllPath) {
        std::vector<std::filesystem::path> candidates;
        if (const char* configured = std::getenv("MAKO_VKD3D_SHADER_PATH");
                configured && *configured != '\0') {
            candidates.emplace_back(configured);
        }
        appendSteamRuntimeCandidates(candidates, shaderDllPath);

        std::vector<std::string> attempts;
        attempts.reserve(candidates.size() + 1);
        for (const auto& candidate : candidates)
            attempts.push_back(candidate.string());
        attempts.emplace_back("libvkd3d-shader.so.1");

        std::string lastError;
        for (const auto& attempt : attempts) {
            void* handle = dlopen(attempt.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
                if (const char* error = dlerror())
                    lastError = error;
                continue;
            }
            Library library(handle);
            try {
                const auto compile = loadSymbol<CompileFunction>(
                    handle, "vkd3d_shader_compile"
                );
                const auto freeCode = loadSymbol<FreeCodeFunction>(
                    handle, "vkd3d_shader_free_shader_code"
                );
                const auto freeMessages = loadSymbol<FreeMessagesFunction>(
                    handle, "vkd3d_shader_free_messages"
                );
                return {
                    .library = std::move(library),
                    .compile = compile,
                    .freeCode = freeCode,
                    .freeMessages = freeMessages,
                    .path = attempt,
                };
            } catch (const std::exception& error) {
                lastError = error.what();
            }
        }

        throw ls::error(
            "unable to load libvkd3d-shader.so.1 for model translation" +
            (lastError.empty() ? std::string{} : ": " + lastError)
        );
    }

    ResourceBinding resourceBinding(
            const DescriptorType type, const uint32_t source,
            const uint32_t destination, const bool image) {
        return {
            .type = type,
            .registerSpace = 0,
            .registerIndex = source,
            .visibility = Visibility::Compute,
            .flags = image ? bindingFlagImage
                : type == DescriptorType::Cbv ? bindingFlagBuffer : 0,
            .binding = {.set = 0, .binding = destination, .count = 1},
        };
    }

    std::vector<ResourceBinding> bindingsFor(
            const backend::detail::ShaderResourceContract& contract) {
        std::vector<ResourceBinding> result;
        const auto add = [&](const DescriptorType type, const size_t count,
                const uint32_t base, const bool image) {
            for (size_t i = 0; i < count; ++i)
                result.push_back(resourceBinding(type, static_cast<uint32_t>(i),
                    base + static_cast<uint32_t>(i), image));
        };
        add(DescriptorType::Cbv, contract.uniformBuffers, 0, false);
        add(DescriptorType::Sampler, contract.samplers, 16, false);
        add(DescriptorType::Srv, contract.sampledImages, 32, true);
        add(DescriptorType::Uav, contract.storageImages, 48, true);
        return result;
    }

    std::vector<uint8_t> translate(
            const Translator& translator,
            const std::vector<uint8_t>& dxbc,
            const backend::detail::ShaderResourceContract& contract,
            const uint32_t storageImageFormat,
            const std::string& sourceName, const bool requireFp32) {
        mako::backend::detail::validateDxbcComputeShader(
            dxbc, sourceName
        );

        const auto bindings = bindingsFor(contract);
        const SpirvTargetInfo target{
            .type = StructureType::SpirvTargetInfo,
            .environment = SpirvEnvironment::Vulkan10,
        };
        const InterfaceInfo interfaceInfo{
            .type = StructureType::InterfaceInfo,
            .next = &target,
            .bindings = bindings.data(),
            .bindingCount = static_cast<uint32_t>(bindings.size()),
        };
        const CompileInfo compileInfo{
            .type = StructureType::CompileInfo,
            .next = &interfaceInfo,
            .source = {.code = dxbc.data(), .size = dxbc.size()},
            .sourceType = SourceType::DxbcTpf,
            .targetType = TargetType::SpirvBinary,
            .logLevel = LogLevel::Warning,
            .sourceName = sourceName.c_str(),
        };

        ShaderCodeOwner output{.freeCode = translator.freeCode};
        MessageOwner messages{.freeMessages = translator.freeMessages};
        const int result = translator.compile(
            &compileInfo, &output.code, &messages.data
        );
        const std::string diagnostic = messages.data
            ? std::string(messages.data) : std::string{};
        if (result != 0 || !output.code.code || output.code.size == 0) {
            throw ls::error(
                "vkd3d-shader failed to translate " + sourceName +
                (diagnostic.empty() ? std::string{} : ": " + diagnostic)
            );
        }

        std::vector<uint8_t> spirv(output.code.size);
        std::memcpy(spirv.data(), output.code.code, output.code.size);
        mako::backend::detail::patchStorageImageFormat(
            spirv, storageImageFormat
        );
        const auto info = mako::backend::detail::validateSpirvComputeShader(
            spirv,
            contract, "translated " + sourceName
        );
        if (requireFp32 && info.declaresFloat16)
            throw ls::error("translated " + sourceName + " unexpectedly requires FP16");
        return spirv;
    }

}

struct mako::backend::detail::DxbcShaderTranslator::Impl {
    Translator translator;
};

mako::backend::detail::DxbcShaderTranslator::DxbcShaderTranslator(
        const std::filesystem::path& dll)
    : impl(std::make_shared<Impl>(Impl{loadTranslator(dll)})) {}

const std::string& mako::backend::detail::DxbcShaderTranslator::path() const {
    return this->impl->translator.path;
}

std::vector<uint8_t> mako::backend::detail::DxbcShaderTranslator::translate(
        const std::vector<uint8_t>& dxbc, const ShaderResourceContract& contract,
        const uint32_t storageImageFormat, const std::string& sourceName,
        const bool requireFp32) const {
    return ::translate(this->impl->translator, dxbc, contract,
        storageImageFormat, sourceName, requireFp32);
}
