/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-backend/ls1.hpp"

#include "dll_reader.hpp"
#include "ls1_spirv_patch.hpp"
#include "mako-common/helpers/errors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
    constexpr uint32_t imageFormatRgba8 = 4;
    constexpr uint32_t imageFormatR8Snorm = 20;

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
            "unable to load libvkd3d-shader.so.1 for LS1 translation" +
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
            const uint32_t sampledImages, const bool sampler,
            const bool constantBuffer) {
        std::vector<ResourceBinding> result;
        result.reserve(sampledImages + 1 + (sampler ? 1 : 0) +
            (constantBuffer ? 1 : 0));
        if (constantBuffer)
            result.push_back(resourceBinding(DescriptorType::Cbv, 0, 0, false));
        if (sampler)
            result.push_back(resourceBinding(DescriptorType::Sampler, 0, 16, false));
        for (uint32_t i = 0; i < sampledImages; ++i)
            result.push_back(resourceBinding(DescriptorType::Srv, i, 32 + i, true));
        result.push_back(resourceBinding(DescriptorType::Uav, 0, 48, true));
        return result;
    }

    std::vector<uint8_t> translate(
            const Translator& translator,
            const std::vector<uint8_t>& dxbc,
            const uint32_t sampledImages,
            const bool sampler,
            const bool constantBuffer,
            const uint32_t storageImageFormat,
            const std::string& sourceName) {
        if (dxbc.size() < 4 || !std::equal(
                dxbc.begin(), dxbc.begin() + 4, "DXBC")) {
            throw ls::error("LS1 resource " + sourceName + " is not DXBC");
        }

        const auto bindings = bindingsFor(
            sampledImages, sampler, constantBuffer
        );
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
                "vkd3d-shader failed to translate LS1 resource " + sourceName +
                (diagnostic.empty() ? std::string{} : ": " + diagnostic)
            );
        }

        std::vector<uint8_t> spirv(output.code.size);
        std::memcpy(spirv.data(), output.code.code, output.code.size);
        mako::backend::detail::patchLs1StorageImageFormat(
            spirv, storageImageFormat
        );
        return spirv;
    }

    const std::vector<uint8_t>& resource(
            const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources,
            const uint32_t id) {
        const auto found = resources.find(id);
        if (found == resources.end())
            throw ls::error("Lossless.dll does not contain LS1 resource " +
                std::to_string(id));
        return found->second;
    }

    std::string shaderCacheKey(
            const std::filesystem::path& shaderDllPath,
            const mako::backend::Ls1Mode mode,
            const uint32_t variant) {
        std::error_code error;
        auto normalized = std::filesystem::weakly_canonical(
            shaderDllPath, error
        );
        if (error) {
            error.clear();
            normalized = std::filesystem::absolute(shaderDllPath, error);
            if (error)
                normalized = shaderDllPath;
        }

        error.clear();
        const auto size = std::filesystem::file_size(shaderDllPath, error);
        const auto stableSize = error ? uintmax_t{0} : size;
        error.clear();
        const auto modified = std::filesystem::last_write_time(
            shaderDllPath, error
        );
        const auto stableModified = error
            ? int64_t{0}
            : static_cast<int64_t>(modified.time_since_epoch().count());
        return normalized.string() + '|' +
            std::to_string(static_cast<uint8_t>(mode)) + '|' +
            std::to_string(variant) + '|' + std::to_string(stableSize) + '|' +
            std::to_string(stableModified);
    }

    mako::backend::Ls1ShaderSet loadUncached(
            const std::filesystem::path& shaderDllPath,
            const mako::backend::Ls1Mode mode,
            const uint32_t variant) {
        const auto resources = mako::backend::extractResourcesFromDLL(
            shaderDllPath
        );
        const auto translator = loadTranslator(shaderDllPath);

        mako::backend::Ls1ShaderSet result{
            .mode = mode,
            .modelVariant = variant,
            .translator = translator.path,
        };
        if (mode == mako::backend::Ls1Mode::Performance) {
            const uint32_t stage1Id = 141 + variant;
            result.stage1 = translate(
                translator, resource(resources, stage1Id), 1, false, true,
                imageFormatR8Snorm, std::to_string(stage1Id)
            );
        } else {
            const uint32_t stage1Id = 147 + variant * 3;
            result.stage1 = translate(
                translator, resource(resources, stage1Id), 1, false, true,
                imageFormatRgba8, std::to_string(stage1Id)
            );
            result.stage2 = translate(
                translator, resource(resources, stage1Id + 1), 1, false,
                false, imageFormatRgba8, std::to_string(stage1Id + 1)
            );
            result.stage3 = translate(
                translator, resource(resources, stage1Id + 2), 1, false, true,
                imageFormatR8Snorm, std::to_string(stage1Id + 2)
            );
        }
        result.reconstruction = translate(
            translator, resource(resources, 146), 2, true, true,
            imageFormatRgba8, "146"
        );
        return result;
    }
}

mako::backend::Ls1ShaderSet mako::backend::loadLs1ShaderSet(
        const std::filesystem::path& shaderDllPath,
        const Ls1Mode mode,
        const float sharpness) {
    if (!std::isfinite(sharpness) || sharpness < 0.0F || sharpness > 1.0F)
        throw ls::error("LS1 sharpness must be between zero and one");

    const uint32_t variant = static_cast<uint32_t>(std::lround(sharpness * 4.0F));
    const auto key = shaderCacheKey(shaderDllPath, mode, variant);

    // Translation is a swapchain-setup operation, never a frame-path
    // operation. Cache the Vulkan-ready payloads in process memory so
    // swapchain recreation and multiple swapchains do not repeatedly parse
    // the licensed DLL or invoke the translator. File identity remains in the
    // key so a replaced DLL cannot reuse stale shaders.
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, Ls1ShaderSet> cache;
    const std::scoped_lock lock(cacheMutex);
    if (const auto found = cache.find(key); found != cache.end())
        return found->second;

    auto result = loadUncached(shaderDllPath, mode, variant);
    cache.emplace(key, result);
    return result;
}
