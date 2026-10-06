/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "model_resource_validation.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mako::backend::detail {

    /// Shared, dynamically loaded vkd3d-shader boundary for licensed models.
    /// Translation and library discovery are setup-only; no payload is saved.
    class DxbcShaderTranslator {
    public:
        explicit DxbcShaderTranslator(const std::filesystem::path& dll);
        [[nodiscard]] const std::string& path() const;
        [[nodiscard]] std::vector<uint8_t> translate(
            const std::vector<uint8_t>& dxbc,
            const ShaderResourceContract& contract,
            uint32_t storageImageFormat, const std::string& sourceName,
            bool requireFp32 = false) const;
    private:
        struct Impl;
        std::shared_ptr<const Impl> impl;
    };

}
