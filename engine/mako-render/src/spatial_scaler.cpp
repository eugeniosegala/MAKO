/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "spatial_scaler.hpp"

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
    std::span<const uint32_t> scalingShader(const VkFormat format) {
        switch (format) {
            case VK_FORMAT_R8G8B8A8_UNORM:
                return mako::layer::embedded::spatialScalingRgba8Spirv;
            case VK_FORMAT_R16G16B16A16_SFLOAT:
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
        virtual void recordCompute(
            const vk::Vulkan&, VkCommandBuffer
        ) const = 0;
        [[nodiscard]] virtual uint32_t modelVariant() const { return 0; }
    };

    class NativeResolutionPipeline final : public Pipeline {
    public:
        NativeResolutionPipeline(const vk::Vulkan& vk,
                const VkExtent2D sourceExtent,
                const VkExtent2D presentationExtent,
                const VkFormat workingFormat) :
            sourceSize(sourceExtent),
            presentationSize(presentationExtent),
            sourceImage(vk, sourceExtent, workingFormat,
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
        void recordCompute(const vk::Vulkan& vk,
                const VkCommandBuffer commandBuffer) const override {
            const std::array barriers{
                imageBarrier(
                    this->sourceImage.handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                ),
                imageBarrier(
                    this->reconstructedImage.handle(), VK_ACCESS_NONE,
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
                this->reconstructedImage.handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &region, VK_FILTER_LINEAR
            );
            const auto outputBarrier = imageBarrier(
                this->reconstructedImage.handle(),
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
                const float sharpness) :
            sourceSize(sourceExtent),
            presentationSize(presentationExtent),
            sourceImage(vk, sourceExtent, workingFormat,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
            reconstructedImage(vk, presentationExtent, workingFormat,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
            shader(vk, scalingShader(workingFormat), 1, 1, 1, 1),
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
        void recordCompute(const vk::Vulkan& vk,
                const VkCommandBuffer commandBuffer) const override {
            const std::array barriers{
                imageBarrier(
                    this->sourceImage.handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL
                ),
                imageBarrier(
                    this->reconstructedImage.handle(), VK_ACCESS_NONE,
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
            const auto descriptor = this->descriptorSet.handle();
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
                this->reconstructedImage.handle(), VK_ACCESS_SHADER_WRITE_BIT,
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
                const mako::backend::Ls1ShaderSet& payloads) :
            sourceSize(sourceExtent),
            presentationSize(presentationExtent),
            performance(payloads.mode == mako::backend::Ls1Mode::Performance),
            variant(payloads.modelVariant),
            sourceImage(vk, sourceExtent, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
            featureImage(vk, doubledExtent(sourceExtent), VK_FORMAT_R8_SNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
            reconstructedImage(vk, presentationExtent,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
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

        void recordCompute(const vk::Vulkan& vk,
                const VkCommandBuffer commandBuffer) const override {
            std::array<VkImageMemoryBarrier, 5> initialBarriers{};
            size_t initialBarrierCount = 0;
            initialBarriers.at(initialBarrierCount++) = imageBarrier(
                this->sourceImage.handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL
            );
            initialBarriers.at(initialBarrierCount++) = imageBarrier(
                this->featureImage.handle(), VK_ACCESS_NONE,
                VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL
            );
            initialBarriers.at(initialBarrierCount++) = imageBarrier(
                this->reconstructedImage.handle(), VK_ACCESS_NONE,
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
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
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
                *this->reconstructionDescriptors, this->presentationSize
            );

            const auto outputBarrier = imageBarrier(
                this->reconstructedImage.handle(), VK_ACCESS_SHADER_WRITE_BIT,
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
            const std::optional<std::filesystem::path>& shaderDllPath) :
        sourceSize(sourceExtent),
        presentationSize(presentationExtent),
        requested(requested),
        active(requested) {
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
                vk, sourceExtent, presentationExtent, workingFormat
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
                    *shaderDllPath, mode, sharpness
                );
                this->translatorPath = payloads.translator;
                this->dllSha256 = payloads.dllSha256;
                this->resourceLayoutSha256 = payloads.resourceLayoutSha256;
                this->pipeline = std::make_unique<Ls1Pipeline>(
                    vk, sourceExtent, presentationExtent, payloads
                );
                return;
            } catch (const std::exception& error) {
                this->fallback = error.what();
                this->active = ls::ScalingMethod::Mako;
            }
        }
        this->pipeline = std::make_unique<MakoPipeline>(
            vk, sourceExtent, presentationExtent, workingFormat, sharpness
        );
    }

    VkExtent2D sourceSize{};
    VkExtent2D presentationSize{};
    ls::ScalingMethod requested{ls::ScalingMethod::Mako};
    ls::ScalingMethod active{ls::ScalingMethod::Mako};
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
        const std::optional<std::filesystem::path>& shaderDllPath) :
    implementation(std::make_unique<Implementation>(
        vk, sourceExtent, presentationExtent, workingFormat,
        requestedMethod, sharpness, shaderDllPath
    )) {}

SpatialScaler::~SpatialScaler() = default;
SpatialScaler::SpatialScaler(SpatialScaler&&) noexcept = default;
SpatialScaler& SpatialScaler::operator=(SpatialScaler&&) noexcept = default;

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

    this->implementation->pipeline->recordCompute(vk, handle);

    std::array<VkImageMemoryBarrier, 2> outputBarriers{};
    size_t outputBarrierCount = 0;
    outputBarriers.at(outputBarrierCount++) = imageBarrier(
        applicationImage, VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    if (frameGenerationSource != VK_NULL_HANDLE) {
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
    if (frameGenerationSource != VK_NULL_HANDLE) {
        vk.df().CmdBlitImage(
            handle, this->implementation->pipeline->output().handle(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            frameGenerationSource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &presentationCopy, VK_FILTER_NEAREST
        );
    }
    vk.df().CmdBlitImage(
        handle, this->implementation->pipeline->output().handle(),
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
