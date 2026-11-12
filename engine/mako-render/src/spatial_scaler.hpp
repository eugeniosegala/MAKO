/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"
#include "mako-backend/mako.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// Application-device spatial reconstruction resources for one swapchain.
    /// The implementation can be the model-free Native Resolution baseline,
    /// MAKO's spatial stage, or LS1 translated from a user-supplied
    /// Lossless.dll. All allocation and translation happens at construction;
    /// record() is allocation-free.
    class SpatialScaler {
    public:
        SpatialScaler(const vk::Vulkan& vk,
            VkExtent2D sourceExtent, VkExtent2D presentationExtent,
            VkFormat workingFormat, ls::ScalingMethod requestedMethod,
            float sharpness,
            const std::optional<std::filesystem::path>& shaderDllPath,
            bool fp16Requested,
            backend::FrameEncoding encoding = backend::FrameEncoding::Sdr8,
            bool hdrReducedPrecision = false);
        ~SpatialScaler();

        SpatialScaler(const SpatialScaler&) = delete;
        SpatialScaler& operator=(const SpatialScaler&) = delete;
        SpatialScaler(SpatialScaler&&) noexcept;
        SpatialScaler& operator=(SpatialScaler&&) noexcept;

        /// Preallocate direct-output bindings for MAKO-owned, presentation-
        /// sized Frame Generation source images. Once configured, record()
        /// writes reconstruction into the selected exported source and copies
        /// that same result back to the real WSI image, avoiding the previous
        /// presentation-sized private-output copy. Clearing the bindings
        /// restores the copy-based path without changing pipeline ordering.
        void configureDirectFrameGenerationOutputs(
            const vk::Vulkan& vk,
            std::span<const std::reference_wrapper<const vk::Image>> outputs);
        void clearDirectFrameGenerationOutputs();
        [[nodiscard]] size_t directFrameGenerationOutputCount() const;

        /// Whether a reconstruction pass can write this swapchain's images
        /// directly: the format supports format-less storage writes on this
        /// device, the surface and image format permit storage usage, and a
        /// direct write stores exactly the pixels the copy-back would at the
        /// method's arithmetic precision. Other swapchains keep the private
        /// output and its copy-back.
        [[nodiscard]] static bool directPresentationOutputSupported(
            const vk::Vulkan& vk, const VkSwapchainCreateInfoKHR& createInfo,
            VkImageUsageFlags surfaceUsage, bool fp16Arithmetic);

        /// Bind a swapchain's images so record() and
        /// recordSourceToPresentation() write them directly, saving the
        /// presentation-sized copy-back. Reconstruction methods need a
        /// storage-capable swapchain; Native Resolution blits into a
        /// transfer-capable one. Returns how many were bound: zero for an
        /// incompatible format.
        size_t configureDirectPresentationOutputs(const vk::Vulkan& vk,
            std::span<const VkImage> images, VkFormat format);
        void clearDirectPresentationOutputs();
        [[nodiscard]] size_t directPresentationOutputCount() const;

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

        /// Reconstruct a source-sized private FG output after the caller's
        /// external GENERAL acquire. The source becomes transfer-readable;
        /// the destination is returned in presentation layout.
        void recordSourceToPresentation(const vk::Vulkan& vk,
            const vk::CommandBuffer& commandBuffer,
            VkImage sourceImage, VkImage presentationImage,
            VkImageLayout sourceLayout =
                VK_IMAGE_LAYOUT_GENERAL,
            VkImageLayout presentationLayout =
                VK_IMAGE_LAYOUT_UNDEFINED) const;

        [[nodiscard]] VkExtent2D sourceExtent() const;
        [[nodiscard]] VkExtent2D presentationExtent() const;
        [[nodiscard]] ls::ScalingMethod requestedMethod() const;
        [[nodiscard]] ls::ScalingMethod activeMethod() const;
        [[nodiscard]] std::string_view fallbackReason() const;
        [[nodiscard]] std::string_view precisionName() const;
        [[nodiscard]] uint32_t ls1ModelVariant() const;
        [[nodiscard]] std::string_view ls1Translator() const;
        [[nodiscard]] std::string_view ls1DllSha256() const;
        [[nodiscard]] std::string_view ls1ResourceLayoutSha256() const;

    private:
        class Implementation;
        std::unique_ptr<Implementation> implementation;
    };

}
