/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "image_transfer.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/vulkan/buffer.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
namespace mako::cli::images {
    [[nodiscard]] VkImageMemoryBarrier imageBarrier(
            const VkImage image, const VkAccessFlags sourceAccess,
            const VkAccessFlags destinationAccess, const VkImageLayout oldLayout,
            const VkImageLayout newLayout) {
        return {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = sourceAccess,
            .dstAccessMask = destinationAccess,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
    }

    void uploadImage(const vk::Vulkan& vk, const vk::Image& image,
            const std::span<const uint8_t> rgba) {
        const size_t expectedBytes = static_cast<size_t>(image.getExtent().width) *
            image.getExtent().height * 4;
        if (rgba.size() != expectedBytes)
            throw ls::error("quality source image has an invalid byte count");
        const vk::Buffer staging{
            vk, rgba.data(), rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        };
        const vk::CommandBuffer command{vk};
        command.begin(vk);
        const auto toTransfer = imageBarrier(
            image.handle(), VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        vk.df().CmdPipelineBarrier(
            command.handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &toTransfer
        );
        const VkBufferImageCopy region{
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageExtent = {
                .width = image.getExtent().width,
                .height = image.getExtent().height,
                .depth = 1,
            },
        };
        vk.df().CmdCopyBufferToImage(
            command.handle(), staging.handle(), image.handle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region
        );
        const auto toGeneral = imageBarrier(
            image.handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL
        );
        vk.df().CmdPipelineBarrier(
            command.handle(), VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &toGeneral
        );
        command.end(vk);
        command.submit(vk);
    }

}
