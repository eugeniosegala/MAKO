/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "../extraction/shader_registry.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/buffer.hpp"
#include "mako-common/vulkan/descriptor_pool.hpp"
#include "mako-common/vulkan/image_memory_pool.hpp"
#include "mako-common/vulkan/sampler.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace mako::backend {
    /// exposed context data
    struct Ctx {
        ls::R<const vk::Vulkan> vk; // safe back reference
        ls::R<const ShaderRegistry> shaders; // safe back reference

        std::shared_ptr<vk::ImageMemoryPool> imageMemoryPool;
        vk::DescriptorPool pool;

        vk::Buffer constantBuffer;
        std::vector<vk::Buffer> constantBuffers;
        vk::Sampler bnbSampler; //!< border, no compare, black
        vk::Sampler bnwSampler; //!< border, no compare, white
        vk::Sampler eabSampler; //!< edge, always compare, black

        VkExtent2D sourceExtent;
        VkExtent2D flowExtent;

        bool highPrecision; //!< model output is backed by an rgba16f image
        bool hdr; //!< model constants select linear HDR/scRGB semantics
        float flow;
        bool perf;
        size_t count;
    };

    /// constant buffer used in shaders
    struct ConstantBuffer {
        std::array<uint32_t, 2> inputOffset;
        uint32_t firstIter;
        uint32_t firstIterS;
        uint32_t advancedColorKind;
        uint32_t hdrSupport;
        float resolutionInvScale;
        float timestamp;
        float uiThreshold;
        std::array<uint32_t, 3> pad;
    };

    /// get a prefilled constant buffer
    /// @param index timestamp index
    /// @param total total amount of images
    /// @param hdr whether the model receives linear HDR/scRGB data
    /// @param invFlow inverted flow scale value
    /// @return prefilled constant buffer
    ConstantBuffer getDefaultConstantBuffer(
        size_t index, size_t total, bool hdr, float invFlow
    );

    /// round down a VkExtent2D
    /// @param extent the extent to shift
    /// @param i the amount to shift by
    /// @return the shifted extent
    VkExtent2D shift_extent(VkExtent2D extent, uint32_t i);

    /// round up a VkExtent2D
    /// @param extent the extent to shift
    /// @param a the amount to add before shifting
    /// @param i the amount to shift by
    /// @return the shifted extent
    VkExtent2D add_shift_extent(VkExtent2D extent, uint32_t a, uint32_t i);

    /// convert a device/vendor id into a hex string
    std::string to_hex_id(uint32_t id);
}
