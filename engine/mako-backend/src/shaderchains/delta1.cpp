/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "delta1.hpp"
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

Delta1::Delta1(const Ctx& ctx, size_t idx,
        const std::vector<vk::Image>& sourceImages0,
        const std::vector<vk::Image>& sourceImages1,
        const std::vector<vk::Image>& temporaryImages0,
        const vk::Image& additionalInput0,
        const vk::Image& additionalInput1,
        const vk::Image& additionalInput2) {
    const size_t m = ctx.perf ? 1 : 2; // multiplier
    const VkExtent2D extent = sourceImages0.at(0).getExtent();
    const auto source1 = backend::requiredPrefix(sourceImages1, m, "Delta1 source 1");
    const auto temporary0 = backend::requiredPrefix(
        temporaryImages0, 2 * m, "Delta1 temporary 0"
    );

    // Reuse the immediately preceding Gamma1 scratch. Gamma1 and Delta1 have
    // the same extent and execute sequentially in one command buffer.
    for (size_t i = 0; i < (2 * m); i++) {
        this->tempImages1.emplace_back(ctx.vk, extent, ctx.imageMemoryPool);
    }
    this->image0.emplace(ctx.vk,
        VkExtent2D { extent.width, extent.height },
        ctx.imageMemoryPool,
        VK_FORMAT_R16G16B16A16_SFLOAT
    );
    this->image1.emplace(ctx.vk,
        VkExtent2D { extent.width, extent.height },
        ctx.imageMemoryPool,
        VK_FORMAT_R16G16B16A16_SFLOAT
    );

    // create descriptor sets
    const auto& shaders = (ctx.perf ?
        ctx.shaders.get().performance : ctx.shaders.get().quality).delta;
    this->sets.reserve(4 + 4);

    this->sets.emplace_back(ManagedShaderBuilder()
        .sampleds(sourceImages0)
        .storages(temporary0)
        .sampler(ctx.bnbSampler)
        .build(ctx.vk, ctx.pool, shaders.at(1)));
    this->sets.emplace_back(ManagedShaderBuilder()
        .sampleds(temporary0)
        .storages(this->tempImages1)
        .sampler(ctx.bnbSampler)
        .build(ctx.vk, ctx.pool, shaders.at(2)));
    this->sets.emplace_back(ManagedShaderBuilder()
        .sampleds(this->tempImages1)
        .storages(temporary0)
        .sampler(ctx.bnbSampler)
        .build(ctx.vk, ctx.pool, shaders.at(3)));
    this->sets.emplace_back(ManagedShaderBuilder()
        .sampleds(temporary0)
        .sampled(additionalInput0)
        .sampled(additionalInput1)
        .storage(*this->image0)
        .sampler(ctx.bnbSampler)
        .sampler(ctx.eabSampler)
        .buffer(ctx.constantBuffers.at(idx))
        .build(ctx.vk, ctx.pool, shaders.at(4)));

    this->sets.emplace_back(ManagedShaderBuilder()
        .sampleds(source1)
        .storages(temporary0.first(m))
        .sampler(ctx.bnbSampler)
        .build(ctx.vk, ctx.pool, shaders.at(6)));
    this->sets.emplace_back(ManagedShaderBuilder()
        .sampleds(temporary0.first(m))
        .storages(this->tempImages1, 0, m)
        .sampler(ctx.bnbSampler)
        .build(ctx.vk, ctx.pool, shaders.at(7)));
    this->sets.emplace_back(ManagedShaderBuilder()
        .sampleds(this->tempImages1, 0, m)
        .storages(temporary0.first(m))
        .sampler(ctx.bnbSampler)
        .build(ctx.vk, ctx.pool, shaders.at(8)));
    this->sets.emplace_back(ManagedShaderBuilder()
        .sampleds(temporary0.first(m))
        .sampled(additionalInput2)
        .storage(*this->image1)
        .sampler(ctx.bnbSampler)
        .sampler(ctx.eabSampler)
        .buffer(ctx.constantBuffers.at(idx))
        .build(ctx.vk, ctx.pool, shaders.at(9)));

    // store dispatch extents
    this->dispatchExtent = backend::add_shift_extent(extent, 7, 3);
}

void Delta1::prepare(std::vector<VkImage>& images) const {
    for (size_t i = 0; i < this->tempImages1.size(); i++) {
        images.push_back(this->tempImages1.at(i).handle());
    }
    images.push_back(this->image0->handle());
    images.push_back(this->image1->handle());
}

void Delta1::render(const vk::Vulkan& vk, const vk::CommandBuffer& cmd) const {
    for (const auto& set : this->sets)
        set.dispatch(vk, cmd, dispatchExtent);
}
