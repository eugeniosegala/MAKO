/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "delta0.hpp"
#include "../helpers/image_prefix.hpp"
#include "../helpers/utils.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <cstddef>
#include <vector>

#include <vulkan/vulkan_core.h>

using namespace mako::backend;

Delta0::Delta0(const Ctx& ctx, size_t idx,
        const std::vector<std::vector<vk::Image>>& sourceImages,
        const std::vector<vk::Image>& outputImages0,
        const std::vector<vk::Image>& outputImages1,
        const vk::Image& additionalInput0,
        const vk::Image& additionalInput1) {
    const size_t m = ctx.perf ? 1 : 2; // multiplier
    const auto outputs0 = backend::requiredPrefix(outputImages0, 3, "Delta0 output 0");
    const auto outputs1 = backend::requiredPrefix(outputImages1, m, "Delta0 output 1");
    const VkExtent2D extent = outputs0.front().getExtent();

    // create descriptor sets
    const auto& shaders = (ctx.perf ?
        ctx.shaders.get().performance : ctx.shaders.get().quality).delta;

    this->sets0.reserve(sourceImages.size());
    for (size_t i = 0; i < sourceImages.size(); i++)
        this->sets0.emplace_back(ManagedShaderBuilder()
            .sampleds(sourceImages.at((i + (sourceImages.size() - 1)) % sourceImages.size()))
            .sampleds(sourceImages.at(i % sourceImages.size()))
            .sampled(additionalInput0)
            .storages(outputs0)
            .sampler(ctx.bnwSampler)
            .sampler(ctx.eabSampler)
            .buffer(ctx.constantBuffers.at(idx))
            .build(ctx.vk, ctx.pool, shaders.at(0)));

    this->sets1.reserve(sourceImages.size());
    for (size_t i = 0; i < sourceImages.size(); i++)
        this->sets1.emplace_back(ManagedShaderBuilder()
            .sampleds(sourceImages.at((i + (sourceImages.size() - 1)) % sourceImages.size()))
            .sampleds(sourceImages.at(i % sourceImages.size()))
            .sampled(additionalInput1)
            .sampled(additionalInput0)
            .storages(outputs1)
            .sampler(ctx.bnwSampler)
            .sampler(ctx.eabSampler)
            .buffer(ctx.constantBuffers.at(idx))
            .build(ctx.vk, ctx.pool, shaders.at(5)));

    // store dispatch extents
    this->dispatchExtent = backend::add_shift_extent(extent, 7, 3);
}

void Delta0::render(const vk::Vulkan& vk, const vk::CommandBuffer& cmd, size_t idx) const {
    this->sets0.at(idx % this->sets0.size()).dispatch(vk, cmd, dispatchExtent);
    this->sets1.at(idx % this->sets1.size()).dispatch(vk, cmd, dispatchExtent);
}
