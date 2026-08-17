/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "../helpers/managed_shader.hpp"
#include "../helpers/utils.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/shader.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <cstddef>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace mako::backend {

    /// Full-resolution colour conversion around the model's linear scRGB space.
    class ColorConversion {
    public:
        ColorConversion(const Ctx& ctx, const vk::Shader& shader,
            const std::pair<vk::Image, vk::Image>& inputs,
            const std::pair<vk::Image, vk::Image>& outputs);

        ColorConversion(const Ctx& ctx, const vk::Shader& shader,
            const std::vector<vk::Image>& inputs,
            const std::vector<vk::Image>& outputs);

        void render(const vk::Vulkan& vk,
            const vk::CommandBuffer& cmd, size_t idx) const;

    private:
        std::vector<ManagedShader> sets;
        VkExtent2D dispatchExtent{};
    };

}
