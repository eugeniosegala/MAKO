/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-backend/ls1.hpp"
#include "dll_reader.hpp"
#include "dxbc_translation.hpp"
#include "model_resources.hpp"
#include "mako-common/helpers/errors.hpp"

#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace mako;

namespace {
    std::string shaderCacheKey(
            const mako::backend::DllResourceArchive& archive,
            const mako::backend::Ls1Mode mode,
            const uint32_t variant) {
        return archive.fileSha256 + '|' +
            std::to_string(static_cast<uint8_t>(mode)) + '|' +
            std::to_string(variant);
    }

    mako::backend::Ls1ShaderSet loadUncached(
            const std::filesystem::path& shaderDllPath,
            const mako::backend::DllResourceArchive& archive,
            const mako::backend::Ls1Mode mode,
            const uint32_t variant) {
        const auto selection = backend::resolveLs1ModelResources(archive, mode, variant);
        const auto spec = backend::detail::ls1ModelSpec(mode, variant);
        const backend::detail::DxbcShaderTranslator translator(shaderDllPath);
        const auto load = [&](const backend::detail::Ls1ShaderSpec& shader) {
            return translator.translate(selection.resource(archive, shader.resourceId),
                shader.contract, shader.storageImageFormat,
                "LS1 resource " + std::to_string(selection.resourceId(shader.resourceId)), true);
        };

        mako::backend::Ls1ShaderSet result{
            .mode = mode,
            .modelVariant = variant,
            .translator = translator.path(),
            .dllSha256 = archive.fileSha256,
            .resourceLayoutSha256 = archive.resourceLayoutSha256,
        };
        result.stage1 = load(spec.stage1);
        if (spec.stage2) result.stage2 = load(*spec.stage2);
        if (spec.stage3) result.stage3 = load(*spec.stage3);
        result.reconstruction = load(spec.reconstruction);
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
    const auto archive = loadDllResourceArchive(shaderDllPath);
    const auto key = shaderCacheKey(*archive, mode, variant);

    // Translation is a swapchain-setup operation, never a frame-path
    // operation. Cache the Vulkan-ready payloads in process memory so
    // swapchain recreation and multiple swapchains do not repeatedly parse
    // the licensed DLL or invoke the translator. File identity remains in the
    // key so a replaced DLL cannot reuse stale shaders. The archive cache also
    // tracks inode, size, mtime, and ctime, while the shader key uses content.
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, Ls1ShaderSet> cache;
    const std::scoped_lock lock(cacheMutex);
    if (const auto found = cache.find(key); found != cache.end())
        return found->second;

    auto result = loadUncached(shaderDllPath, *archive, mode, variant);
    cache.emplace(key, result);
    return result;
}
