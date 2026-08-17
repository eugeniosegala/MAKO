/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "color_conversion.hpp"
#include "../helpers/utils.hpp"

#include <stdexcept>

using namespace mako::backend;

namespace {
    ManagedShader buildConversionSet(const Ctx& ctx,
            const vk::Shader& shader,
            const vk::Image& input, const vk::Image& output) {
        return ManagedShaderBuilder()
            .sampled(input)
            .storage(output)
            .sampler(ctx.eabSampler)
            .build(ctx.vk, ctx.pool, shader);
    }
}

ColorConversion::ColorConversion(const Ctx& ctx, const vk::Shader& shader,
        const std::pair<vk::Image, vk::Image>& inputs,
        const std::pair<vk::Image, vk::Image>& outputs) {
    this->sets.reserve(2);
    this->sets.emplace_back(buildConversionSet(
        ctx, shader, inputs.first, outputs.first
    ));
    this->sets.emplace_back(buildConversionSet(
        ctx, shader, inputs.second, outputs.second
    ));
    this->dispatchExtent = backend::add_shift_extent(ctx.sourceExtent, 7, 3);
}

ColorConversion::ColorConversion(const Ctx& ctx, const vk::Shader& shader,
        const std::vector<vk::Image>& inputs,
        const std::vector<vk::Image>& outputs) {
    if (inputs.size() != outputs.size())
        throw std::invalid_argument("colour conversion image counts do not match");
    if (inputs.empty())
        throw std::invalid_argument("colour conversion requires at least one image");

    this->sets.reserve(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i)
        this->sets.emplace_back(buildConversionSet(
            ctx, shader, inputs.at(i), outputs.at(i)
        ));
    this->dispatchExtent = backend::add_shift_extent(ctx.sourceExtent, 7, 3);
}

void ColorConversion::render(const vk::Vulkan& vk,
        const vk::CommandBuffer& cmd, const size_t idx) const {
    this->sets.at(idx % this->sets.size()).dispatch(vk, cmd, this->dispatchExtent);
}
