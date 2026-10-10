/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "spatial_scaler.hpp"
#include "color_pipeline.hpp"

#include "mako-backend/dll_inspection.hpp"
#include "mako-backend/ls1.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/buffer.hpp"
#include "mako-common/vulkan/descriptor_pool.hpp"
#include "mako-common/vulkan/descriptor_set.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/sampler.hpp"
#include "mako-common/vulkan/shader.hpp"
#include "shaders/spatial_scaling_spirv.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

using namespace mako::layer;

namespace {
    std::span<const uint32_t> scalingShader(const VkFormat format, const bool fp16) {
        switch (format) {
            case VK_FORMAT_R8G8B8A8_UNORM:
                if (fp16)
                    return mako::layer::embedded::spatialScalingRgba8Fp16Spirv;
                return mako::layer::embedded::spatialScalingRgba8Spirv;
            case VK_FORMAT_R16G16B16A16_SFLOAT:
                if (fp16)
                    return mako::layer::embedded::spatialScalingRgba16fFp16Spirv;
                return mako::layer::embedded::spatialScalingRgba16fSpirv;
            default:
                throw ls::vulkan_error(
                    "unsupported spatial-scaling working format"
                );
        }
    }

    VkExtent2D doubledExtent(const VkExtent2D extent) {
        if (extent.width > UINT32_MAX / 2 || extent.height > UINT32_MAX / 2)
            throw ls::vulkan_error("LS1 feature extent overflows Vulkan limits");
        return {extent.width * 2, extent.height * 2};
    }

    VkImageMemoryBarrier imageBarrier(
            const VkImage image,
            const VkAccessFlags sourceAccess,
            const VkAccessFlags destinationAccess,
            const VkImageLayout oldLayout,
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

    VkImageBlit blitRegion(
            const VkExtent2D source, const VkExtent2D destination) {
        return {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .srcOffsets = {
                {0, 0, 0},
                {
                    static_cast<int32_t>(source.width),
                    static_cast<int32_t>(source.height),
                    1,
                },
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .dstOffsets = {
                {0, 0, 0},
                {
                    static_cast<int32_t>(destination.width),
                    static_cast<int32_t>(destination.height),
                    1,
                },
            },
        };
    }

    void bindAndDispatch(const vk::Vulkan& vk,
            const VkCommandBuffer commandBuffer,
            const vk::Shader& shader,
            const vk::DescriptorSet& descriptorSet,
            const VkExtent2D extent) {
        vk.df().CmdBindPipeline(
            commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader.pipeline()
        );
        const auto descriptor = descriptorSet.handle();
        vk.df().CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            shader.pipelinelayout(), 0, 1, &descriptor, 0, nullptr
        );
        vk.df().CmdDispatch(
            commandBuffer, (extent.width + 15) / 16,
            (extent.height + 15) / 16, 1
        );
    }

    class Pipeline {
    public:
        virtual ~Pipeline() = default;
        [[nodiscard]] virtual const vk::Image& input() const = 0;
        [[nodiscard]] virtual const vk::Image& output() const = 0;
        virtual void configureDirectOutputs(
            const vk::Vulkan&,
            std::span<const std::reference_wrapper<const vk::Image>>) = 0;
        virtual void clearDirectOutputs() = 0;
        [[nodiscard]] virtual size_t directOutputCount() const = 0;
        [[nodiscard]] virtual bool hasDirectOutput(VkImage) const = 0;
        virtual void recordCompute(
            const vk::Vulkan&, VkCommandBuffer,
            VkImage directOutput = VK_NULL_HANDLE
        ) const = 0;
        [[nodiscard]] virtual uint32_t modelVariant() const { return 0; }
    };

    [[nodiscard]] std::optional<size_t> directOutputIndex(
            const std::span<const VkImage> outputs,
            const VkImage image) {
        for (size_t index = 0; index < outputs.size(); ++index) {
            if (outputs[index] == image)
                return index;
        }
        return std::nullopt;
    }

    class NativeResolutionPipeline final : public Pipeline {
    public:
        NativeResolutionPipeline(const vk::Vulkan& vk,
                const VkExtent2D sourceExtent,
                const VkExtent2D presentationExtent,
                const VkFormat workingFormat, const VkFormat inputFormat) :
            sourceSize(sourceExtent),
            presentationSize(presentationExtent),
            sourceImage(vk, sourceExtent, inputFormat,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
            reconstructedImage(vk, presentationExtent, workingFormat,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {}

        [[nodiscard]] const vk::Image& input() const override {
            return this->sourceImage;
        }
        [[nodiscard]] const vk::Image& output() const override {
            return this->reconstructedImage;
        }
        void configureDirectOutputs(const vk::Vulkan&,
                const std::span<const std::reference_wrapper<const vk::Image>>
                    outputs) override {
            std::vector<VkImage> handles;
            handles.reserve(outputs.size());
            for (const auto& output : outputs)
                handles.push_back(output.get().handle());
            this->directOutputs = std::move(handles);
        }
        void clearDirectOutputs() override {
            this->directOutputs.clear();
        }
        [[nodiscard]] size_t directOutputCount() const override {
            return this->directOutputs.size();
        }
        [[nodiscard]] bool hasDirectOutput(
                const VkImage image) const override {
            return directOutputIndex(this->directOutputs, image).has_value();
        }
        void recordCompute(const vk::Vulkan& vk,
                const VkCommandBuffer commandBuffer,
                const VkImage directOutput) const override {
            const VkImage outputImage = directOutput == VK_NULL_HANDLE
                ? this->reconstructedImage.handle() : directOutput;
            const std::array barriers{
                imageBarrier(
                    this->sourceImage.handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                ),
                imageBarrier(
                    outputImage, VK_ACCESS_NONE,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                ),
            };
            vk.df().CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr, 0, nullptr,
                static_cast<uint32_t>(barriers.size()), barriers.data()
            );
            const auto region = blitRegion(
                this->sourceSize, this->presentationSize
            );
            vk.df().CmdBlitImage(
                commandBuffer,
                this->sourceImage.handle(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                outputImage,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &region, VK_FILTER_LINEAR
            );
            const auto outputBarrier = imageBarrier(
                outputImage,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            vk.df().CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &outputBarrier
            );
        }

    private:
        VkExtent2D sourceSize{};
        VkExtent2D presentationSize{};
        vk::Image sourceImage;
        vk::Image reconstructedImage;
        std::vector<VkImage> directOutputs;
    };

    class MakoPipeline final : public Pipeline {
    public:
        struct alignas(16) Parameters {
            float sourceWidth;
            float sourceHeight;
            float presentationWidth;
            float presentationHeight;
            float sharpness;
            float reserved0{0.0F};
            float reserved1{0.0F};
            float reserved2{0.0F};
        };

        MakoPipeline(const vk::Vulkan& vk,
                const VkExtent2D sourceExtent,
                const VkExtent2D presentationExtent,
                const VkFormat workingFormat,
                const float sharpness, const bool fp16, const VkFormat inputFormat) :
            sourceSize(sourceExtent),
            presentationSize(presentationExtent),
            sourceImage(vk, sourceExtent, inputFormat,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
            reconstructedImage(vk, presentationExtent, workingFormat,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
            shader(vk, scalingShader(workingFormat, fp16), 1, 1, 1, 1),
            descriptorPool(vk, {
                .sets = 1,
                .uniform_buffers = 1,
                .samplers = 1,
                .sampled_images = 1,
                .storage_images = 1,
            }),
            sampler(vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                VK_COMPARE_OP_NEVER, false),
            parameterBuffer(vk, Parameters{
                .sourceWidth = static_cast<float>(sourceExtent.width),
                .sourceHeight = static_cast<float>(sourceExtent.height),
                .presentationWidth =
                    static_cast<float>(presentationExtent.width),
                .presentationHeight =
                    static_cast<float>(presentationExtent.height),
                .sharpness = sharpness,
            }),
            descriptorSet(vk, this->descriptorPool, this->shader,
                std::vector<ls::R<const vk::Image>>{
                    std::cref(this->sourceImage)
                },
                std::vector<ls::R<const vk::Image>>{
                    std::cref(this->reconstructedImage)
                },
                std::vector<ls::R<const vk::Sampler>>{
                    std::cref(this->sampler)
                },
                std::vector<ls::R<const vk::Buffer>>{
                    std::cref(this->parameterBuffer)
                }) {}

        [[nodiscard]] const vk::Image& input() const override {
            return this->sourceImage;
        }
        [[nodiscard]] const vk::Image& output() const override {
            return this->reconstructedImage;
        }
        void configureDirectOutputs(const vk::Vulkan& vk,
                const std::span<const std::reference_wrapper<const vk::Image>>
                    outputs) override {
            std::optional<vk::DescriptorPool> replacementPool;
            std::vector<vk::DescriptorSet> replacementSets;
            std::vector<VkImage> replacementHandles;
            if (!outputs.empty()) {
                const auto count = static_cast<uint32_t>(outputs.size());
                replacementPool.emplace(vk, vk::Limits{
                    .sets = count,
                    .uniform_buffers = count,
                    .samplers = count,
                    .sampled_images = count,
                    .storage_images = count,
                });
                replacementSets.reserve(outputs.size());
                replacementHandles.reserve(outputs.size());
                for (const auto& output : outputs) {
                    replacementSets.emplace_back(
                        vk, *replacementPool, this->shader,
                        std::vector<ls::R<const vk::Image>>{
                            std::cref(this->sourceImage)
                        },
                        std::vector<ls::R<const vk::Image>>{
                            std::cref(output.get())
                        },
                        std::vector<ls::R<const vk::Sampler>>{
                            std::cref(this->sampler)
                        },
                        std::vector<ls::R<const vk::Buffer>>{
                            std::cref(this->parameterBuffer)
                        }
                    );
                    replacementHandles.push_back(output.get().handle());
                }
            }

            this->directDescriptorSets.clear();
            this->directDescriptorPool.reset();
            this->directDescriptorPool = std::move(replacementPool);
            this->directDescriptorSets = std::move(replacementSets);
            this->directOutputs = std::move(replacementHandles);
        }
        void clearDirectOutputs() override {
            this->directDescriptorSets.clear();
            this->directDescriptorPool.reset();
            this->directOutputs.clear();
        }
        [[nodiscard]] size_t directOutputCount() const override {
            return this->directOutputs.size();
        }
        [[nodiscard]] bool hasDirectOutput(
                const VkImage image) const override {
            return directOutputIndex(this->directOutputs, image).has_value();
        }
        void recordCompute(const vk::Vulkan& vk,
                const VkCommandBuffer commandBuffer,
                const VkImage directOutput) const override {
            const auto directIndex = directOutput == VK_NULL_HANDLE
                ? std::optional<size_t>{}
                : directOutputIndex(this->directOutputs, directOutput);
            const VkImage outputImage = directIndex
                ? directOutput : this->reconstructedImage.handle();
            const auto& outputDescriptors = directIndex
                ? this->directDescriptorSets.at(*directIndex)
                : this->descriptorSet;
            const std::array barriers{
                imageBarrier(
                    this->sourceImage.handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL
                ),
                imageBarrier(
                    outputImage, VK_ACCESS_NONE,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL
                ),
            };
            vk.df().CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                0, nullptr, 0, nullptr,
                static_cast<uint32_t>(barriers.size()), barriers.data()
            );
            vk.df().CmdBindPipeline(
                commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                this->shader.pipeline()
            );
            const auto descriptor = outputDescriptors.handle();
            vk.df().CmdBindDescriptorSets(
                commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                this->shader.pipelinelayout(), 0, 1, &descriptor, 0, nullptr
            );
            vk.df().CmdDispatch(
                commandBuffer,
                (this->presentationSize.width + 7) / 8,
                (this->presentationSize.height + 7) / 8, 1
            );
            const auto outputBarrier = imageBarrier(
                outputImage, VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            vk.df().CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                1, &outputBarrier
            );
        }

    private:
        VkExtent2D sourceSize{};
        VkExtent2D presentationSize{};
        vk::Image sourceImage;
        vk::Image reconstructedImage;
        vk::Shader shader;
        vk::DescriptorPool descriptorPool;
        vk::Sampler sampler;
        vk::Buffer parameterBuffer;
        vk::DescriptorSet descriptorSet;
        std::optional<vk::DescriptorPool> directDescriptorPool;
        std::vector<vk::DescriptorSet> directDescriptorSets;
        std::vector<VkImage> directOutputs;
    };

    class Ls1Pipeline final : public Pipeline {
    public:
        struct alignas(16) Parameters {
            uint32_t sourceWidth;
            uint32_t sourceHeight;
            uint32_t capturedWidth;
            uint32_t capturedHeight;
            uint32_t sourceOffsetX{0};
            uint32_t sourceOffsetY{0};
            uint32_t gammaPreprocess{0};
            uint32_t reserved{0};
            uint32_t outputWidth;
            uint32_t outputHeight;
            uint32_t outputOffsetX{0};
            uint32_t outputOffsetY{0};
        };
        static_assert(sizeof(Parameters) == 48);

        Ls1Pipeline(const vk::Vulkan& vk,
                const VkExtent2D sourceExtent,
                const VkExtent2D presentationExtent,
                const mako::backend::Ls1ShaderSet& payloads,
                const VkFormat workingFormat, const bool shaderBoundary = false,
                const VkFormat inputFormat = VK_FORMAT_UNDEFINED) :
            sourceSize(sourceExtent),
            presentationSize(presentationExtent),
            performance(payloads.mode == mako::backend::Ls1Mode::Performance),
            variant(payloads.modelVariant),
            sourceImage(vk, sourceExtent,
                inputFormat == VK_FORMAT_UNDEFINED ? workingFormat : inputFormat,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    (shaderBoundary ? VK_IMAGE_USAGE_STORAGE_BIT : 0U)),
            featureImage(vk, doubledExtent(sourceExtent), VK_FORMAT_R8_SNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
            reconstructedImage(vk, presentationExtent, workingFormat,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    (shaderBoundary ? VK_IMAGE_USAGE_SAMPLED_BIT : 0U)),
            stage1(vk, payloads.stage1, 1, 1, 1, 0),
            reconstruction(vk, payloads.reconstruction, 2, 1, 1, 1),
            descriptorPool(vk, {
                .sets = this->performance ? 2U : 4U,
                .uniform_buffers = this->performance ? 2U : 3U,
                .samplers = 1,
                .sampled_images = this->performance ? 3U : 5U,
                .storage_images = this->performance ? 2U : 4U,
            }),
            sampler(vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                VK_COMPARE_OP_NEVER, false),
            parameterBuffer(vk, Parameters{
                .sourceWidth = sourceExtent.width,
                .sourceHeight = sourceExtent.height,
                .capturedWidth = sourceExtent.width,
                .capturedHeight = sourceExtent.height,
                .outputWidth = presentationExtent.width,
                .outputHeight = presentationExtent.height,
            }) {
            if (!this->performance) {
                this->intermediateA.emplace(
                    vk, sourceExtent, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                );
                this->intermediateB.emplace(
                    vk, sourceExtent, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                );
                this->stage2.emplace(vk, payloads.stage2, 1, 1, 0, 0);
                this->stage3.emplace(vk, payloads.stage3, 1, 1, 1, 0);
            }

            const auto& firstOutput = this->performance
                ? this->featureImage : *this->intermediateA;
            this->stage1Descriptors.emplace(
                vk, this->descriptorPool, this->stage1,
                std::vector<ls::R<const vk::Image>>{
                    std::cref(this->sourceImage)
                },
                std::vector<ls::R<const vk::Image>>{
                    std::cref(firstOutput)
                }, std::vector<ls::R<const vk::Sampler>>{},
                std::vector<ls::R<const vk::Buffer>>{
                    std::cref(this->parameterBuffer)
                }
            );
            if (!this->performance) {
                this->stage2Descriptors.emplace(
                    vk, this->descriptorPool, *this->stage2,
                    std::vector<ls::R<const vk::Image>>{
                        std::cref(*this->intermediateA)
                    },
                    std::vector<ls::R<const vk::Image>>{
                        std::cref(*this->intermediateB)
                    },
                    std::vector<ls::R<const vk::Sampler>>{},
                    std::vector<ls::R<const vk::Buffer>>{}
                );
                this->stage3Descriptors.emplace(
                    vk, this->descriptorPool, *this->stage3,
                    std::vector<ls::R<const vk::Image>>{
                        std::cref(*this->intermediateB)
                    },
                    std::vector<ls::R<const vk::Image>>{
                        std::cref(this->featureImage)
                    }, std::vector<ls::R<const vk::Sampler>>{},
                    std::vector<ls::R<const vk::Buffer>>{
                        std::cref(this->parameterBuffer)
                    }
                );
            }
            this->reconstructionDescriptors.emplace(
                vk, this->descriptorPool, this->reconstruction,
                std::vector<ls::R<const vk::Image>>{
                    std::cref(this->featureImage),
                    std::cref(this->sourceImage),
                },
                std::vector<ls::R<const vk::Image>>{
                    std::cref(this->reconstructedImage)
                },
                std::vector<ls::R<const vk::Sampler>>{
                    std::cref(this->sampler)
                },
                std::vector<ls::R<const vk::Buffer>>{
                    std::cref(this->parameterBuffer)
                }
            );
        }

        [[nodiscard]] const vk::Image& input() const override {
            return this->sourceImage;
        }
        [[nodiscard]] const vk::Image& output() const override {
            return this->reconstructedImage;
        }
        [[nodiscard]] uint32_t modelVariant() const override {
            return this->variant;
        }

        void configureDirectOutputs(const vk::Vulkan& vk,
                const std::span<const std::reference_wrapper<const vk::Image>>
                    outputs) override {
            std::optional<vk::DescriptorPool> replacementPool;
            std::vector<vk::DescriptorSet> replacementSets;
            std::vector<VkImage> replacementHandles;
            if (!outputs.empty()) {
                const auto count = static_cast<uint32_t>(outputs.size());
                replacementPool.emplace(vk, vk::Limits{
                    .sets = count,
                    .uniform_buffers = count,
                    .samplers = count,
                    .sampled_images = count * 2,
                    .storage_images = count,
                });
                replacementSets.reserve(outputs.size());
                replacementHandles.reserve(outputs.size());
                for (const auto& output : outputs) {
                    replacementSets.emplace_back(
                        vk, *replacementPool, this->reconstruction,
                        std::vector<ls::R<const vk::Image>>{
                            std::cref(this->featureImage),
                            std::cref(this->sourceImage),
                        },
                        std::vector<ls::R<const vk::Image>>{
                            std::cref(output.get())
                        },
                        std::vector<ls::R<const vk::Sampler>>{
                            std::cref(this->sampler)
                        },
                        std::vector<ls::R<const vk::Buffer>>{
                            std::cref(this->parameterBuffer)
                        }
                    );
                    replacementHandles.push_back(output.get().handle());
                }
            }

            this->directDescriptorSets.clear();
            this->directDescriptorPool.reset();
            this->directDescriptorPool = std::move(replacementPool);
            this->directDescriptorSets = std::move(replacementSets);
            this->directOutputs = std::move(replacementHandles);
        }
        void clearDirectOutputs() override {
            this->directDescriptorSets.clear();
            this->directDescriptorPool.reset();
            this->directOutputs.clear();
        }
        [[nodiscard]] size_t directOutputCount() const override {
            return this->directOutputs.size();
        }
        [[nodiscard]] bool hasDirectOutput(
                const VkImage image) const override {
            return directOutputIndex(this->directOutputs, image).has_value();
        }

        void recordCompute(const vk::Vulkan& vk,
                const VkCommandBuffer commandBuffer,
                const VkImage directOutput) const override {
            recordWithInput(vk, commandBuffer, directOutput, false);
        }
        void recordWithInput(const vk::Vulkan& vk,
                const VkCommandBuffer commandBuffer,
                const VkImage directOutput, const bool shaderInput,
                const bool shaderOutput = false) const {
            const auto directIndex = directOutput == VK_NULL_HANDLE
                ? std::optional<size_t>{}
                : directOutputIndex(this->directOutputs, directOutput);
            const VkImage outputImage = directIndex
                ? directOutput : this->reconstructedImage.handle();
            const auto& outputDescriptors = directIndex
                ? this->directDescriptorSets.at(*directIndex)
                : *this->reconstructionDescriptors;
            std::array<VkImageMemoryBarrier, 5> initialBarriers{};
            size_t initialBarrierCount = 0;
            initialBarriers.at(initialBarrierCount++) = imageBarrier(
                this->sourceImage.handle(),
                shaderInput ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                shaderInput ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL
            );
            initialBarriers.at(initialBarrierCount++) = imageBarrier(
                this->featureImage.handle(), VK_ACCESS_NONE,
                VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL
            );
            initialBarriers.at(initialBarrierCount++) = imageBarrier(
                outputImage, VK_ACCESS_NONE,
                VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL
            );
            if (!this->performance) {
                initialBarriers.at(initialBarrierCount++) = imageBarrier(
                    this->intermediateA->handle(), VK_ACCESS_NONE,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL
                );
                initialBarriers.at(initialBarrierCount++) = imageBarrier(
                    this->intermediateB->handle(), VK_ACCESS_NONE,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL
                );
            }
            vk.df().CmdPipelineBarrier(
                commandBuffer,
                shaderInput ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                0, nullptr, 0, nullptr,
                static_cast<uint32_t>(initialBarrierCount),
                initialBarriers.data()
            );

            bindAndDispatch(
                vk, commandBuffer, this->stage1,
                *this->stage1Descriptors, this->sourceSize
            );
            if (!this->performance) {
                this->shaderWriteToReadBarrier(
                    vk, commandBuffer, this->intermediateA->handle()
                );
                bindAndDispatch(
                    vk, commandBuffer, *this->stage2,
                    *this->stage2Descriptors, this->sourceSize
                );
                this->shaderWriteToReadBarrier(
                    vk, commandBuffer, this->intermediateB->handle()
                );
                bindAndDispatch(
                    vk, commandBuffer, *this->stage3,
                    *this->stage3Descriptors, this->sourceSize
                );
            }
            this->shaderWriteToReadBarrier(
                vk, commandBuffer, this->featureImage.handle()
            );
            bindAndDispatch(
                vk, commandBuffer, this->reconstruction,
                outputDescriptors, this->presentationSize
            );

            const auto outputBarrier = imageBarrier(
                outputImage, VK_ACCESS_SHADER_WRITE_BIT,
                shaderOutput ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL,
                shaderOutput ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            vk.df().CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                shaderOutput ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr,
                1, &outputBarrier
            );
        }

    private:
        static void shaderWriteToReadBarrier(const vk::Vulkan& vk,
                const VkCommandBuffer commandBuffer, const VkImage image) {
            const auto barrier = imageBarrier(
                image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL
            );
            vk.df().CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier
            );
        }

        VkExtent2D sourceSize{};
        VkExtent2D presentationSize{};
        bool performance{false};
        uint32_t variant{0};
        vk::Image sourceImage;
        std::optional<vk::Image> intermediateA;
        std::optional<vk::Image> intermediateB;
        vk::Image featureImage;
        vk::Image reconstructedImage;
        vk::Shader stage1;
        std::optional<vk::Shader> stage2;
        std::optional<vk::Shader> stage3;
        vk::Shader reconstruction;
        vk::DescriptorPool descriptorPool;
        vk::Sampler sampler;
        vk::Buffer parameterBuffer;
        std::optional<vk::DescriptorSet> stage1Descriptors;
        std::optional<vk::DescriptorSet> stage2Descriptors;
        std::optional<vk::DescriptorSet> stage3Descriptors;
        std::optional<vk::DescriptorSet> reconstructionDescriptors;
        std::optional<vk::DescriptorPool> directDescriptorPool;
        std::vector<vk::DescriptorSet> directDescriptorSets;
        std::vector<VkImage> directOutputs;
    };
    // LS1's feature network operates on bounded display code values. Keep its
    // HDR input/output at RGBA16F precision and convert linear scRGB around the
    // network instead of clipping highlights into an SDR UNORM image.
    class ScRgbLs1Pipeline final : public Pipeline {
    public:
        ScRgbLs1Pipeline(const vk::Vulkan& vk, VkExtent2D source,
                VkExtent2D presentation, const mako::backend::Ls1ShaderSet& payloads) :
            sourceSize(source), presentationSize(presentation),
            inner(vk, source, presentation, payloads, VK_FORMAT_R16G16B16A16_SFLOAT, true),
            sourceImage(vk, source, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
            outputImage(vk, presentation, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
            encode(vk, mako::backend::hdrColorConversionShader(true), 1, 1, 0, 1),
            decode(vk, mako::backend::hdrColorConversionShader(false), 1, 1, 0, 1),
            pool(vk, {.sets = 2, .samplers = 2, .sampled_images = 2, .storage_images = 2}),
            sampler(vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_COMPARE_OP_NEVER, false),
            encodeSet(vk, pool, encode,
                {std::cref(sourceImage)}, {std::cref(inner.input())}, {std::cref(sampler)}, {}),
            decodeSet(vk, pool, decode,
                {std::cref(inner.output())}, {std::cref(outputImage)}, {std::cref(sampler)}, {}) {}

        const vk::Image& input() const override { return sourceImage; }
        const vk::Image& output() const override { return outputImage; }
        uint32_t modelVariant() const override { return inner.modelVariant(); }
        void configureDirectOutputs(const vk::Vulkan& vk,
                std::span<const std::reference_wrapper<const vk::Image>> outputs) override {
            std::optional<vk::DescriptorPool> replacementPool;
            std::vector<vk::DescriptorSet> replacementSets;
            std::vector<VkImage> replacementHandles;
            if (!outputs.empty()) {
                const auto count = static_cast<uint32_t>(outputs.size());
                replacementPool.emplace(vk, vk::Limits{
                    .sets = count, .samplers = count,
                    .sampled_images = count, .storage_images = count,
                });
                replacementSets.reserve(outputs.size());
                replacementHandles.reserve(outputs.size());
                for (const auto& output : outputs) {
                    replacementSets.emplace_back(vk, *replacementPool, decode,
                        std::vector<ls::R<const vk::Image>>{std::cref(inner.output())},
                        std::vector<ls::R<const vk::Image>>{std::cref(output.get())},
                        std::vector<ls::R<const vk::Sampler>>{std::cref(sampler)},
                        std::vector<ls::R<const vk::Buffer>>{});
                    replacementHandles.push_back(output.get().handle());
                }
            }
            clearDirectOutputs();
            directDescriptorPool = std::move(replacementPool);
            directDescriptorSets = std::move(replacementSets);
            directOutputs = std::move(replacementHandles);
        }
        void clearDirectOutputs() override {
            directDescriptorSets.clear();
            directDescriptorPool.reset();
            directOutputs.clear();
        }
        size_t directOutputCount() const override { return directOutputs.size(); }
        bool hasDirectOutput(VkImage image) const override {
            return directOutputIndex(directOutputs, image).has_value();
        }

        void recordCompute(const vk::Vulkan& vk, VkCommandBuffer commandBuffer,
                VkImage directOutput) const override {
            const auto directIndex = directOutputIndex(directOutputs, directOutput);
            const auto destination = directIndex ? directOutput : outputImage.handle();
            const auto& descriptors = directIndex ? directDescriptorSets.at(*directIndex) : decodeSet;
            const std::array inputBarriers{
                imageBarrier(sourceImage.handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL),
                imageBarrier(inner.input().handle(), VK_ACCESS_NONE,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL),
            };
            vk.df().CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                inputBarriers.size(), inputBarriers.data());
            dispatch(vk, commandBuffer, encode, encodeSet, sourceSize);
            inner.recordWithInput(vk, commandBuffer, VK_NULL_HANDLE, true, true);
            const auto outputBarrier = imageBarrier(destination, VK_ACCESS_NONE,
                VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL);
            vk.df().CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                1, &outputBarrier);
            // Decode directly into the exported source when provisioned, just
            // like the other scalers. The normal fallback image remains owned
            // for FG-off and resource-replacement transitions.
            dispatch(vk, commandBuffer, decode, descriptors, presentationSize);
            const auto finalBarrier = imageBarrier(destination, VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            vk.df().CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &finalBarrier);
        }
    private:
        static void dispatch(const vk::Vulkan& vk, VkCommandBuffer commandBuffer,
                const vk::Shader& shader, const vk::DescriptorSet& set, VkExtent2D extent) {
            vk.df().CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader.pipeline());
            const auto descriptor = set.handle();
            vk.df().CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                shader.pipelinelayout(), 0, 1, &descriptor, 0, nullptr);
            vk.df().CmdDispatch(commandBuffer, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
        }
        VkExtent2D sourceSize;
        VkExtent2D presentationSize;
        Ls1Pipeline inner;
        vk::Image sourceImage;
        vk::Image outputImage;
        vk::Shader encode;
        vk::Shader decode;
        vk::DescriptorPool pool;
        vk::Sampler sampler;
        vk::DescriptorSet encodeSet;
        vk::DescriptorSet decodeSet;
        std::optional<vk::DescriptorPool> directDescriptorPool;
        std::vector<vk::DescriptorSet> directDescriptorSets;
        std::vector<VkImage> directOutputs;
    };
}

class SpatialScaler::Implementation {
public:
    Implementation(const vk::Vulkan& vk,
            const VkExtent2D sourceExtent,
            const VkExtent2D presentationExtent,
            const VkFormat workingFormat,
            const ls::ScalingMethod requested,
            const float sharpness,
            const std::optional<std::filesystem::path>& shaderDllPath,
            const bool fp16Requested, const mako::backend::FrameEncoding encoding,
            const bool hdrReducedPrecision) :
        sourceSize(sourceExtent),
        presentationSize(presentationExtent),
        requested(requested),
        active(requested) {
        const bool packedSupported = hdrReducedPrecision &&
            (encoding == mako::backend::FrameEncoding::Hdr10Pq ||
             encoding == mako::backend::FrameEncoding::Hdr10PqPacked) &&
            vk.supportsOptimalTilingFormatFeatures(
                VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
        const auto inputFormat = hdrScalingInputFormat(
            encoding, workingFormat, hdrReducedPrecision, packedSupported);
        if (encoding == mako::backend::FrameEncoding::Hdr10Pq ||
                encoding == mako::backend::FrameEncoding::Hdr10PqPacked) {
            std::clog << "MAKO Renderer: HDR scaling precision: input_format="
                      << inputFormat << "; output_format=" << workingFormat
                      << "; compact_input=" << (inputFormat != workingFormat) << '\n';
        }
        // The spatial role is constructed at the scaling-engine startup
        // boundary, while model selection remains live. Prime the immutable
        // DLL archive here so the first later LS1 selection does not place
        // file hashing and structural inspection inside vkQueuePresentKHR.
        // Native Resolution and MAKO do not depend on the licensed input, so
        // a missing or incompatible DLL must not prevent their construction.
        if (shaderDllPath && !ls::licensedScalingModelRequested(requested)) {
            try {
                static_cast<void>(
                    mako::backend::inspectLosslessDll(*shaderDllPath)
                );
            } catch (const std::exception&) {
            }
        }
        if (requested == ls::ScalingMethod::Native) {
            this->pipeline = std::make_unique<NativeResolutionPipeline>(
                vk, sourceExtent, presentationExtent, workingFormat, inputFormat
            );
            return;
        }
        const bool ls1Requested =
            ls::licensedScalingModelRequested(requested);
        if (ls1Requested) {
            try {
                if (!shaderDllPath)
                    throw ls::error("Lossless.dll was not found");
                const auto mode = requested == ls::ScalingMethod::Ls1Performance
                    ? mako::backend::Ls1Mode::Performance
                    : mako::backend::Ls1Mode::Quality;
                auto payloads = mako::backend::loadLs1ShaderSet(
                    *shaderDllPath, mode, sharpness, workingFormat == VK_FORMAT_R16G16B16A16_SFLOAT
                );
                this->translatorPath = payloads.translator;
                this->dllSha256 = payloads.dllSha256;
                this->resourceLayoutSha256 = payloads.resourceLayoutSha256;
                if (encoding == mako::backend::FrameEncoding::ScRgbLinear) {
                    this->pipeline = std::make_unique<ScRgbLs1Pipeline>(
                        vk, sourceExtent, presentationExtent, payloads);
                } else {
                    this->pipeline = std::make_unique<Ls1Pipeline>(
                        vk, sourceExtent, presentationExtent, payloads, workingFormat,
                        false, inputFormat);
                }
                return;
            } catch (const std::exception& error) {
                this->fallback = error.what();
                this->active = ls::ScalingMethod::Mako;
            }
        }
        // LS1 always uses FP32. The global precision choice applies only to
        // MAKO Scaler, including fallback after an LS1 construction failure.
        if (fp16Requested && !vk.supportsFP16()) {
            this->active = ls::ScalingMethod::Native;
            this->fallback = "scaling FP16 requested but shaderFloat16 is not enabled on the application device; Native Resolution blit is active; select FP32 for compute scaling";
            // The lower swapchain may already have presentation-sized images.
            // Preserve reconstruction of the source rectangle rather than
            // exposing an incomplete real frame or substituting FP32 compute.
            this->pipeline = std::make_unique<NativeResolutionPipeline>(
                vk, sourceExtent, presentationExtent, workingFormat, inputFormat);
            return;
        }
        this->fp16 = fp16Requested;
        this->pipeline = std::make_unique<MakoPipeline>(
            vk, sourceExtent, presentationExtent, workingFormat, sharpness, fp16Requested, inputFormat
        );
    }

    VkExtent2D sourceSize{};
    VkExtent2D presentationSize{};
    ls::ScalingMethod requested{ls::ScalingMethod::Mako};
    ls::ScalingMethod active{ls::ScalingMethod::Mako};
    bool fp16{false};
    std::string fallback;
    std::string translatorPath;
    std::string dllSha256;
    std::string resourceLayoutSha256;
    std::unique_ptr<Pipeline> pipeline;
};

SpatialScaler::SpatialScaler(const vk::Vulkan& vk,
        const VkExtent2D sourceExtent,
        const VkExtent2D presentationExtent,
        const VkFormat workingFormat,
        const ls::ScalingMethod requestedMethod,
        const float sharpness,
        const std::optional<std::filesystem::path>& shaderDllPath,
        const bool fp16Requested, const mako::backend::FrameEncoding encoding,
        const bool hdrReducedPrecision) :
    implementation(std::make_unique<Implementation>(
        vk, sourceExtent, presentationExtent, workingFormat,
        requestedMethod, sharpness, shaderDllPath, fp16Requested, encoding, hdrReducedPrecision
    )) {}

SpatialScaler::~SpatialScaler() = default;
SpatialScaler::SpatialScaler(SpatialScaler&&) noexcept = default;
SpatialScaler& SpatialScaler::operator=(SpatialScaler&&) noexcept = default;

void SpatialScaler::configureDirectFrameGenerationOutputs(
        const vk::Vulkan& vk,
        const std::span<
            const std::reference_wrapper<const vk::Image>> outputs) {
    for (const auto& output : outputs) {
        const auto extent = output.get().getExtent();
        if (extent.width != this->implementation->presentationSize.width ||
                extent.height !=
                    this->implementation->presentationSize.height) {
            throw ls::vulkan_error(
                "direct Frame Generation output extent does not match "
                "spatial presentation extent"
            );
        }
    }
    this->implementation->pipeline->configureDirectOutputs(vk, outputs);
}

void SpatialScaler::clearDirectFrameGenerationOutputs() {
    this->implementation->pipeline->clearDirectOutputs();
}

size_t SpatialScaler::directFrameGenerationOutputCount() const {
    return this->implementation->pipeline->directOutputCount();
}

VkExtent2D SpatialScaler::sourceExtent() const {
    return this->implementation->sourceSize;
}

VkExtent2D SpatialScaler::presentationExtent() const {
    return this->implementation->presentationSize;
}

ls::ScalingMethod SpatialScaler::requestedMethod() const {
    return this->implementation->requested;
}

ls::ScalingMethod SpatialScaler::activeMethod() const {
    return this->implementation->active;
}

std::string_view SpatialScaler::fallbackReason() const {
    return this->implementation->fallback;
}

std::string_view SpatialScaler::precisionName() const {
    if (this->implementation->active == ls::ScalingMethod::Native)
        return "blit";
    return this->implementation->fp16 ? "fp16" : "fp32";
}

uint32_t SpatialScaler::ls1ModelVariant() const {
    return this->implementation->pipeline->modelVariant();
}

std::string_view SpatialScaler::ls1Translator() const {
    return this->implementation->translatorPath;
}

std::string_view SpatialScaler::ls1DllSha256() const {
    return this->implementation->dllSha256;
}

std::string_view SpatialScaler::ls1ResourceLayoutSha256() const {
    return this->implementation->resourceLayoutSha256;
}

void SpatialScaler::record(const vk::Vulkan& vk,
        const vk::CommandBuffer& commandBuffer,
        const VkImage applicationImage,
        const VkImage frameGenerationSource,
        const VkImageLayout applicationLayout) const {
    const auto handle = commandBuffer.handle();
    const std::array inputBarriers{
        imageBarrier(
            applicationImage, VK_ACCESS_MEMORY_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, applicationLayout,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        ),
        imageBarrier(
            this->implementation->pipeline->input().handle(), VK_ACCESS_NONE,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        ),
    };
    vk.df().CmdPipelineBarrier(
        handle, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
        static_cast<uint32_t>(inputBarriers.size()), inputBarriers.data()
    );

    const auto sourceCopy = blitRegion(
        this->implementation->sourceSize, this->implementation->sourceSize
    );
    vk.df().CmdBlitImage(
        handle, applicationImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        this->implementation->pipeline->input().handle(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &sourceCopy, VK_FILTER_NEAREST
    );

    const bool directFrameGenerationOutput =
        frameGenerationSource != VK_NULL_HANDLE &&
        this->implementation->pipeline->hasDirectOutput(
            frameGenerationSource
        );
    this->implementation->pipeline->recordCompute(
        vk, handle,
        directFrameGenerationOutput
            ? frameGenerationSource : VK_NULL_HANDLE
    );

    std::array<VkImageMemoryBarrier, 2> outputBarriers{};
    size_t outputBarrierCount = 0;
    outputBarriers.at(outputBarrierCount++) = imageBarrier(
        applicationImage, VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    if (frameGenerationSource != VK_NULL_HANDLE &&
            !directFrameGenerationOutput) {
        outputBarriers.at(outputBarrierCount++) = imageBarrier(
            frameGenerationSource, VK_ACCESS_NONE,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
    }
    vk.df().CmdPipelineBarrier(
        handle, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
        static_cast<uint32_t>(outputBarrierCount), outputBarriers.data()
    );

    const auto presentationCopy = blitRegion(
        this->implementation->presentationSize,
        this->implementation->presentationSize
    );
    if (frameGenerationSource != VK_NULL_HANDLE &&
            !directFrameGenerationOutput) {
        vk.df().CmdBlitImage(
            handle, this->implementation->pipeline->output().handle(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            frameGenerationSource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &presentationCopy, VK_FILTER_NEAREST
        );
    }
    vk.df().CmdBlitImage(
        handle,
        directFrameGenerationOutput
            ? frameGenerationSource
            : this->implementation->pipeline->output().handle(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        applicationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &presentationCopy, VK_FILTER_NEAREST
    );

    const auto applicationBarrier = imageBarrier(
        applicationImage, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        applicationLayout
    );
    vk.df().CmdPipelineBarrier(
        handle, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
        0, nullptr, 0, nullptr, 1, &applicationBarrier
    );
}

void SpatialScaler::recordSourceToPresentation(const vk::Vulkan& vk,
        const vk::CommandBuffer& commandBuffer,
        const VkImage sourceImage, const VkImage presentationImage,
        const VkImageLayout sourceLayout,
        const VkImageLayout presentationLayout) const {
    const auto handle = commandBuffer.handle();
    const std::array inputBarriers{
        imageBarrier(
            sourceImage, VK_ACCESS_MEMORY_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, sourceLayout,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        ),
        imageBarrier(
            this->implementation->pipeline->input().handle(), VK_ACCESS_NONE,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        ),
    };
    vk.df().CmdPipelineBarrier(
        handle, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
        static_cast<uint32_t>(inputBarriers.size()), inputBarriers.data()
    );

    const auto sourceCopy = blitRegion(
        this->implementation->sourceSize, this->implementation->sourceSize
    );
    vk.df().CmdBlitImage(
        handle, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        this->implementation->pipeline->input().handle(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &sourceCopy, VK_FILTER_NEAREST
    );

    this->implementation->pipeline->recordCompute(vk, handle);

    const auto presentationBarrier = imageBarrier(
        presentationImage,
        presentationLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_ACCESS_NONE : VK_ACCESS_MEMORY_READ_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, presentationLayout,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    vk.df().CmdPipelineBarrier(
        handle, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
        1, &presentationBarrier
    );

    const auto presentationCopy = blitRegion(
        this->implementation->presentationSize,
        this->implementation->presentationSize
    );
    vk.df().CmdBlitImage(
        handle, this->implementation->pipeline->output().handle(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        presentationImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &presentationCopy, VK_FILTER_NEAREST
    );

    const auto finalBarrier = imageBarrier(
        presentationImage, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );
    vk.df().CmdPipelineBarrier(
        handle, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
        0, nullptr, 0, nullptr, 1, &finalBarrier
    );
}
