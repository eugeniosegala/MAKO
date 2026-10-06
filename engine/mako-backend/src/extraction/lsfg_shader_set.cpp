/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "lsfg_shader_set.hpp"
#include "dxbc_translation.hpp"
#include "spirv_fp16.hpp"
#include "mako-common/helpers/errors.hpp"

#include <mutex>
#include <string>
#include <utility>

struct mako::backend::LsfgTranslationCache {
    struct Entry {
        std::shared_ptr<const TranslatedLsfgShaders> shaders;
        size_t convertedFp16Stages{0};
    };
    std::map<std::pair<std::string, uint32_t>,
        Entry> tables;
};

namespace {
    // Metadata/translation only: never taken during dispatch or presentation.
    std::mutex translationMutex;
}

const std::vector<uint8_t>& mako::backend::LsfgShaderSet::resource(
        const DllResourceArchive& archive, const uint32_t logicalId,
        const bool performance) const {
    if (this->translated)
        return this->translated->at(detail::lsfgDxbcResourceId(logicalId, performance));
    return this->selection.resource(archive,
        detail::lsfgResourceId(logicalId, this->fp16, performance));
}

mako::backend::LsfgShaderSet mako::backend::loadLsfgShaderSet(
        const DllResourceArchive& archive, const std::filesystem::path& dll,
        const bool fp16, const std::optional<bool> performance) {
    try {
        return {.selection = resolveLsfgModelResources(archive, fp16, performance),
            .fp16 = fp16, .translated = {}};
    } catch (const ls::error& nativeError) {
        if (fp16) {
            if (!detail::experimentalLsfgFp16Available())
                throw ls::error(std::string("LSFG FP16 requested but unavailable; "
                    "select FP32 or a DLL with a supported native FP16 model: ") +
                    nativeError.what());
            try {
                // Use the same validated FP32 owner for native and DirectX
                // sources. No conversion is performed for an FP16-off choice.
                const auto source = loadLsfgShaderSet(archive, dll, false, performance);
                const auto identity = source.translated
                    ? detail::DxbcShaderTranslator(dll).path() : std::string("native");
                const auto key = std::pair{"fp16/" + identity,
                    performance ? (*performance ? 1U : 0U) : 2U};
                const std::scoped_lock lock(translationMutex);
                if (!archive.lsfgTranslationCache)
                    archive.lsfgTranslationCache = std::make_shared<LsfgTranslationCache>();
                auto& cache = archive.lsfgTranslationCache->tables;
                auto found = cache.find(key);
                if (found == cache.end()) {
                    auto shaders = std::make_shared<TranslatedLsfgShaders>();
                    size_t halfStages = 0;
                    for (const bool mode : {false, true}) {
                        if (performance && *performance != mode)
                            continue;
                        for (const auto& spec : detail::lsfgShaderSpecs(mode)) {
                            const auto id = detail::lsfgDxbcResourceId(spec.logicalId, mode);
                            auto converted = detail::convertLsfgShaderToFp16(
                                source.resource(archive, spec.logicalId, mode), spec.contract,
                                "LSFG resource " + std::to_string(id));
                            halfStages += converted.hasHalfArithmetic ? 1U : 0U;
                            shaders->emplace(id, std::move(converted.bytes));
                        }
                    }
                    if (halfStages == 0)
                        throw ls::error("LSFG conversion produced no explicit FP16 arithmetic");
                    found = cache.emplace(key, LsfgTranslationCache::Entry{
                        std::move(shaders), halfStages}).first;
                }
                return {.selection = source.selection, .fp16 = true,
                    .translated = found->second.shaders,
                    .convertedFp16Stages = found->second.convertedFp16Stages};
            } catch (const std::exception& error) {
                throw ls::error(std::string("LSFG experimental FP16 fallback failed: ") + error.what());
            }
        }
        try {
            const auto selection = resolveLsfgDxbcModelResources(archive, performance);
            const detail::DxbcShaderTranslator translator(dll);
            const auto key = std::pair{translator.path(),
                performance ? (*performance ? 1U : 0U) : 2U};
            const std::scoped_lock lock(translationMutex);
            if (!archive.lsfgTranslationCache)
                archive.lsfgTranslationCache = std::make_shared<LsfgTranslationCache>();
            auto& cache = archive.lsfgTranslationCache->tables;
            auto found = cache.find(key);
            if (found == cache.end()) {
                auto shaders = std::make_shared<TranslatedLsfgShaders>();
                for (const bool mode : {false, true}) {
                    if (performance && *performance != mode)
                        continue;
                    for (const auto& spec : detail::lsfgShaderSpecs(mode)) {
                        const auto id = detail::lsfgDxbcResourceId(spec.logicalId, mode);
                        shaders->emplace(id, translator.translate(
                            selection.resource(archive, id), spec.contract,
                            detail::lsfgStorageImageFormat(spec.logicalId),
                            "LSFG DirectX resource " +
                                std::to_string(selection.resourceId(id)), true));
                    }
                }
                found = cache.emplace(key, LsfgTranslationCache::Entry{std::move(shaders), 0}).first;
            }
            // DirectX translation is available only for an explicit FP32 choice.
            return {.selection = selection, .fp16 = false, .translated = found->second.shaders};
        } catch (const std::exception& error) {
            throw ls::error(std::string(nativeError.what()) +
                "; LSFG DirectX fallback failed: " + error.what());
        }
    }
}
