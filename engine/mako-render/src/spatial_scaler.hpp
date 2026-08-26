/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// Application-device spatial reconstruction resources for one swapchain.
    /// The implementation can be MAKO's native stage or LS1 translated from a
    /// user-supplied Lossless.dll. All allocation and translation happens at
    /// construction; record() is allocation-free.
    class SpatialScaler {
    public:
        SpatialScaler(const vk::Vulkan& vk,
            VkExtent2D sourceExtent, VkExtent2D presentationExtent,
            VkFormat workingFormat, ls::ScalingMethod requestedMethod,
            float sharpness,
            const std::optional<std::filesystem::path>& shaderDllPath);
        ~SpatialScaler();

        SpatialScaler(const SpatialScaler&) = delete;
        SpatialScaler& operator=(const SpatialScaler&) = delete;
        SpatialScaler(SpatialScaler&&) noexcept;
        SpatialScaler& operator=(SpatialScaler&&) noexcept;

        /// Reconstruct the application's low-resolution rectangle, write the
        /// native-resolution result back into its image, and optionally copy
        /// the same result to MAKO's exported FG source. Production WSI uses
        /// the default presentation layout; offscreen validation supplies its
        /// actual application-image layout explicitly.
        void record(const vk::Vulkan& vk,
            const vk::CommandBuffer& commandBuffer,
            VkImage applicationImage,
            VkImage frameGenerationSource = VK_NULL_HANDLE,
            VkImageLayout applicationLayout =
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) const;

        [[nodiscard]] VkExtent2D sourceExtent() const;
        [[nodiscard]] VkExtent2D presentationExtent() const;
        [[nodiscard]] ls::ScalingMethod requestedMethod() const;
        [[nodiscard]] ls::ScalingMethod activeMethod() const;
        [[nodiscard]] std::string_view fallbackReason() const;
        [[nodiscard]] uint32_t ls1ModelVariant() const;
        [[nodiscard]] std::string_view ls1Translator() const;

    private:
        class Implementation;
        std::unique_ptr<Implementation> implementation;
    };

}
