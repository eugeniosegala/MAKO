/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "model_resources.hpp"
#include "model_resource_validation.hpp"
#include "mako-common/helpers/errors.hpp"

#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace {
    struct ResolutionOutcome {
        std::optional<int64_t> offset;
        std::string reason;
    };

    // Protect only setup-time metadata, not shader execution or frame work.
    std::mutex resolutionMutex;
    constexpr size_t maximumDiscoveryResources = 4096U;
}

struct mako::backend::ModelResolutionCache {
    // First key: LSFG FP32/FP16 or LS1 Q/P. Second: mode/variant or all.
    std::map<std::pair<uint32_t, uint32_t>, ResolutionOutcome> selections;
    std::optional<ResolutionOutcome> relocatedLsfg;
    std::optional<ResolutionOutcome> relocatedLs1;
};

namespace {
    using namespace mako::backend;

    template<typename Action>
    ResolutionOutcome attempt(Action action) {
        try {
            return {.offset = action(), .reason = {}};
        } catch (const ls::error& error) {
            return {.offset = std::nullopt, .reason = error.what()};
        }
    }

    int64_t requireResolution(const ResolutionOutcome& result) {
        if (!result.offset)
            throw ls::error(result.reason);
        return *result.offset;
    }

    template<typename Action>
    ModelResourceSelection cachedResolution(const DllResourceArchive& archive,
            const std::pair<uint32_t, uint32_t> key, Action action) {
        const std::scoped_lock lock(resolutionMutex);
        if (!archive.modelResolutionCache)
            archive.modelResolutionCache = std::make_shared<ModelResolutionCache>();
        auto& cache = *archive.modelResolutionCache;
        auto found = cache.selections.find(key);
        if (found == cache.selections.end())
            found = cache.selections.emplace(key, attempt([&] {
                return action(cache);
            })).first;
        return {requireResolution(found->second)};
    }

    template<typename Action>
    void visitLs1(const Ls1Mode mode, const uint32_t variant, Action action) {
        const auto spec = detail::ls1ModelSpec(mode, variant);
        action(spec.reconstruction);
        action(spec.stage1);
        if (spec.stage2) action(*spec.stage2);
        if (spec.stage3) action(*spec.stage3);
    }

    bool validateLsfg(const DllResourceArchive& archive,
            const ModelResourceSelection selection,
            const bool fp16, const bool performance) {
        bool declaresFloat16 = false;
        for (const auto& spec : detail::lsfgShaderSpecs(performance)) {
            const auto id = detail::lsfgResourceId(spec.logicalId, fp16, performance);
            const auto info = detail::validateSpirvComputeShader(
                selection.resource(archive, id), spec.contract,
                "LSFG resource " + std::to_string(selection.resourceId(id)));
            if (selection.idOffset != 0 && !info.exactBindings)
                throw ls::error("relocated LSFG table has incompatible discovery bindings");
            declaresFloat16 = declaresFloat16 || info.declaresFloat16;
        }
        return declaresFloat16;
    }

    void validateLs1(const DllResourceArchive& archive,
            const ModelResourceSelection selection, const Ls1Mode mode,
            const uint32_t variant, const bool requireReflection) {
        visitLs1(mode, variant, [&](const detail::Ls1ShaderSpec& spec) {
            const auto& data = selection.resource(archive, spec.resourceId);
            const auto name = "LS1 resource " +
                std::to_string(selection.resourceId(spec.resourceId));
            detail::validateDxbcResourceBindings(data, spec.contract, name, requireReflection);
        });
    }

    template<typename Validate>
    int64_t discoverTable(const DllResourceArchive& archive,
            const uint32_t anchor, const std::string& family, Validate validate) {
        if (archive.resources.size() > maximumDiscoveryResources)
            throw ls::error(family + " resource discovery exceeds its bounded inventory");
        std::optional<int64_t> selected;
        for (const auto& [id, data] : archive.resources) {
            static_cast<void>(data);
            const int64_t offset = static_cast<int64_t>(id) - anchor;
            if (offset == 0)
                continue; // The caller already inspected the canonical model.
            const auto candidate = attempt([&] {
                validate(ModelResourceSelection{offset});
                return offset;
            });
            if (!candidate.offset)
                continue;
            if (selected)
                throw ls::error("ambiguous relocated " + family + " resource tables");
            selected = offset;
        }
        if (!selected)
            throw ls::error("no supported relocated " + family + " resource table");
        return *selected;
    }

    int64_t discoverLsfg(const DllResourceArchive& archive) {
        // Both precision lanes and both modes must retain their relative
        // positions. A single stage's bindings cannot establish its role.
        const uint32_t anchor = detail::lsfgResourceId(255U, true, false);
        return discoverTable(archive, anchor, "LSFG", [&](const auto selection) {
            for (const bool fp16 : {true, false}) {
                bool declaresFloat16 = false;
                for (const bool performance : {false, true})
                    declaresFloat16 = validateLsfg(archive, selection,
                        fp16, performance) || declaresFloat16;
                if (declaresFloat16 != fp16)
                    throw ls::error("relocated LSFG table has unproven precision order");
            }
        });
    }

    int64_t discoverLs1(const DllResourceArchive& archive) {
        const auto anchor = detail::ls1ModelSpec(Ls1Mode::Quality, 0).reconstruction;
        return discoverTable(archive, anchor.resourceId, "LS1", [&](const auto selection) {
            // Use the distinctive reconstruction interface as an early filter.
            detail::validateDxbcResourceBindings(
                selection.resource(archive, anchor.resourceId), anchor.contract,
                "LS1 reconstruction candidate");
            for (const auto mode : {Ls1Mode::Quality, Ls1Mode::Performance})
                for (uint32_t variant = 0; variant < 5U; ++variant)
                    validateLs1(archive, selection, mode, variant, true);
        });
    }

    template<typename Discover>
    int64_t relocatedAfterFailure(const ls::error& original,
            std::optional<ResolutionOutcome>& cached, Discover discover) {
        if (!cached)
            cached = attempt(discover);
        if (!cached->offset)
            throw ls::error(std::string(original.what()) + "; " + cached->reason);
        return *cached->offset;
    }
}

uint32_t mako::backend::ModelResourceSelection::resourceId(
        const uint32_t canonicalId) const {
    const int64_t base = canonicalId;
    if (this->idOffset < -base ||
            this->idOffset > std::numeric_limits<uint32_t>::max() - base)
        throw ls::error("relocated model resource ID is out of range");
    return static_cast<uint32_t>(base + this->idOffset);
}

const std::vector<uint8_t>& mako::backend::ModelResourceSelection::resource(
        const DllResourceArchive& archive, const uint32_t canonicalId) const {
    const uint32_t id = this->resourceId(canonicalId);
    const auto found = archive.resources.find(id);
    if (found == archive.resources.end())
        throw ls::error("model DLL does not contain resource " + std::to_string(id));
    return found->second;
}

mako::backend::ModelResourceSelection mako::backend::resolveLsfgModelResources(
        const DllResourceArchive& archive, const bool fp16,
        const std::optional<bool> performance) {
    return cachedResolution(archive, {fp16 ? 1U : 0U,
            performance ? (*performance ? 1U : 0U) : 2U}, [&](ModelResolutionCache& cache) {
        try {
            for (const bool mode : {false, true})
                if (!performance || *performance == mode)
                    static_cast<void>(validateLsfg(archive, {}, fp16, mode));
            return int64_t{0};
        } catch (const ls::error& error) {
            return relocatedAfterFailure(error, cache.relocatedLsfg,
                [&] { return discoverLsfg(archive); });
        }
    });
}

mako::backend::ModelResourceSelection mako::backend::resolveLs1ModelResources(
        const DllResourceArchive& archive, const Ls1Mode mode,
        const std::optional<uint32_t> variant) {
    if (variant && *variant >= 5U)
        throw ls::error("invalid LS1 model variant");
    return cachedResolution(archive, {mode == Ls1Mode::Quality ? 4U : 5U,
            variant.value_or(5U)}, [&](ModelResolutionCache& cache) {
        try {
            const uint32_t begin = variant.value_or(0U);
            const uint32_t end = variant ? begin + 1U : 5U;
            for (uint32_t i = begin; i < end; ++i)
                validateLs1(archive, {}, mode, i, false);
            return int64_t{0};
        } catch (const ls::error& error) {
            return relocatedAfterFailure(error, cache.relocatedLs1,
                [&] { return discoverLs1(archive); });
        }
    });
}
