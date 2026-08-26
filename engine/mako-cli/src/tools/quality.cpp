/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "quality.hpp"
#include "mako-backend/mako.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/paths.hpp"
#include "mako-common/quality/image_quality.hpp"
#include "mako-common/vulkan/buffer.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/fence.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/timeline_semaphore.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "spatial_scaler.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

using namespace mako::cli;
using namespace mako::cli::quality;

namespace {
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

    [[nodiscard]] VkPhysicalDevice selectDevice(
            const vk::VulkanInstanceFuncs& functions,
            const std::vector<VkPhysicalDevice>& devices,
            const std::optional<std::string>& requestedName) {
        for (const auto device : devices) {
            VkPhysicalDeviceProperties2 properties{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            };
            functions.GetPhysicalDeviceProperties2(device, &properties);
            const std::array<char, 256> name = std::to_array(
                properties.properties.deviceName
            );
            if (requestedName && std::string(name.data()) == *requestedName)
                return device;
            if (!requestedName && properties.properties.vendorID == 0x1002)
                return device;
        }
        if (!requestedName)
            return devices.front();
        throw ls::error("failed to find specified GPU: " + *requestedName);
    }

    [[nodiscard]] std::string selectedDeviceName(const vk::Vulkan& vk) {
        VkPhysicalDeviceProperties2 properties{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        };
        vk.fi().GetPhysicalDeviceProperties2(vk.physdev(), &properties);
        const std::array<char, 256> name = std::to_array(
            properties.properties.deviceName
        );
        return name.data();
    }

    [[nodiscard]] vk::Vulkan makeVulkan(
            const std::optional<std::string>& requestedGpu,
            const std::string& applicationName) {
        return {
            applicationName, vk::version{2, 0, 0},
            applicationName, vk::version{2, 0, 0},
            [&requestedGpu](const vk::VulkanInstanceFuncs& functions,
                    const std::vector<VkPhysicalDevice>& devices) {
                return selectDevice(functions, devices, requestedGpu);
            }
        };
    }

    [[nodiscard]] mako::quality::QualitySceneKind sceneKind(
            const std::string& name) {
        const auto kind = mako::quality::qualitySceneFromName(name);
        if (!kind)
            throw ls::error("unknown quality scene: " + name);
        return *kind;
    }

    [[nodiscard]] std::optional<std::filesystem::path> configuredDll(
            const std::optional<std::string>& overridePath,
            const bool required) {
        if (overridePath)
            return std::filesystem::path(*overridePath);
        const auto configPath = ls::findConfigurationFile();
        if (std::filesystem::exists(configPath)) {
            const ls::ConfigFile configuration{configPath};
            if (configuration.global().dll)
                return std::filesystem::path(*configuration.global().dll);
        }
        if (!required)
            return std::nullopt;
        return std::filesystem::path(ls::findShaderDll());
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

    void uploadSpatialSource(const vk::Vulkan& vk, const vk::Image& image,
            const VkExtent2D sourceExtent, const std::span<const uint8_t> rgba) {
        const size_t expectedBytes = static_cast<size_t>(sourceExtent.width) *
            sourceExtent.height * 4;
        if (rgba.size() != expectedBytes ||
                sourceExtent.width > image.getExtent().width ||
                sourceExtent.height > image.getExtent().height)
            throw ls::error("spatial quality source image has an invalid extent");
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
                .width = sourceExtent.width,
                .height = sourceExtent.height,
                .depth = 1,
            },
        };
        vk.df().CmdCopyBufferToImage(
            command.handle(), staging.handle(), image.handle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region
        );
        const auto toGeneral = imageBarrier(
            image.handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL
        );
        vk.df().CmdPipelineBarrier(
            command.handle(), VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
            0, nullptr, 0, nullptr, 1, &toGeneral
        );
        command.end(vk);
        command.submit(vk);
    }

    void submitSpatialEndpoint(const vk::Vulkan& vk,
            const mako::layer::SpatialScaler& scaler,
            const vk::Image& applicationImage,
            const vk::Image& frameGenerationSource,
            const VkExtent2D sourceExtent,
            const std::span<const uint8_t> rgba,
            const vk::TimelineSemaphore& sync,
            const uint64_t signalValue) {
        uploadSpatialSource(vk, applicationImage, sourceExtent, rgba);
        const vk::CommandBuffer command{vk};
        command.begin(vk);
        scaler.record(
            vk,
            command,
            applicationImage.handle(),
            frameGenerationSource.handle(),
            VK_IMAGE_LAYOUT_GENERAL
        );
        command.end(vk);
        command.submit(
            vk, {}, VK_NULL_HANDLE, 0,
            {}, sync.handle(), signalValue
        );
    }

    [[nodiscard]] std::vector<uint8_t> downloadImage(
            const vk::Vulkan& vk, const vk::Image& image,
            const VkImageLayout oldLayout, const VkAccessFlags sourceAccess,
            const VkPipelineStageFlags sourceStage,
            const VkSemaphore waitTimelineSemaphore = VK_NULL_HANDLE,
            const uint64_t waitValue = 0) {
        const size_t byteCount = static_cast<size_t>(image.getExtent().width) *
            image.getExtent().height * 4;
        const std::vector<uint8_t> zeroes(byteCount);
        const vk::Buffer staging{
            vk, zeroes.data(), zeroes.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT
        };
        const vk::CommandBuffer command{vk};
        command.begin(vk);
        const auto toTransfer = imageBarrier(
            image.handle(), sourceAccess, VK_ACCESS_TRANSFER_READ_BIT,
            oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        vk.df().CmdPipelineBarrier(
            command.handle(), sourceStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
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
        vk.df().CmdCopyImageToBuffer(
            command.handle(), image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging.handle(), 1, &region
        );
        command.end(vk);
        const vk::Fence completion{vk};
        command.submit(
            vk, {}, waitTimelineSemaphore, waitValue,
            {}, VK_NULL_HANDLE, 0, completion.handle()
        );
        if (!completion.wait(vk))
            throw ls::error("timed out while reading the quality image");
        return staging.read(vk);
    }

    void writePpm(const std::filesystem::path& path,
            const uint32_t width, const uint32_t height,
            const std::span<const uint8_t> rgba) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
            throw ls::error("failed to create quality artifact: " + path.string());
        output << "P6\n" << width << ' ' << height << "\n255\n";
        for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel) {
            const size_t offset = pixel * 4;
            output.write(reinterpret_cast<const char*>(rgba.data() + offset), 3);
        }
        if (!output.good())
            throw ls::error("failed to write quality artifact: " + path.string());
    }

    void writeArtifacts(const std::filesystem::path& directory,
            const mako::quality::RegressionScene& scene,
            const std::span<const uint8_t> generated) {
        std::filesystem::create_directories(directory);
        writePpm(directory / "previous.ppm", scene.width, scene.height, scene.previous);
        writePpm(directory / "current.ppm", scene.width, scene.height, scene.current);
        writePpm(directory / "reference.ppm", scene.width, scene.height, scene.reference);
        writePpm(directory / "generated.ppm", scene.width, scene.height, generated);
    }

    void writeArtifacts(const std::filesystem::path& directory,
            const mako::quality::SpatialRegressionScene& scene,
            const std::span<const uint8_t> generated) {
        std::filesystem::create_directories(directory);
        writePpm(
            directory / "source.ppm",
            scene.sourceWidth,
            scene.sourceHeight,
            scene.source
        );
        writePpm(
            directory / "reference.ppm",
            scene.presentationWidth,
            scene.presentationHeight,
            scene.reference
        );
        writePpm(
            directory / "generated.ppm",
            scene.presentationWidth,
            scene.presentationHeight,
            generated
        );
    }

    void writeArtifacts(const std::filesystem::path& directory,
            const mako::quality::CombinedRegressionScene& scene,
            const std::span<const uint8_t> generated) {
        std::filesystem::create_directories(directory);
        writePpm(
            directory / "previous.ppm",
            scene.sourceWidth,
            scene.sourceHeight,
            scene.previous
        );
        writePpm(
            directory / "current.ppm",
            scene.sourceWidth,
            scene.sourceHeight,
            scene.current
        );
        writePpm(
            directory / "reference.ppm",
            scene.presentationWidth,
            scene.presentationHeight,
            scene.reference
        );
        writePpm(
            directory / "generated.ppm",
            scene.presentationWidth,
            scene.presentationHeight,
            generated
        );
    }

    void printMetrics(const mako::quality::ImageQualityMetrics& metrics,
            const mako::quality::ImageQualityThresholds& thresholds) {
        std::cout << "  mean absolute error: " << metrics.meanAbsoluteError
            << " / " << thresholds.maximumMeanAbsoluteError << '\n'
            << "  focus-region error: "
            << metrics.focusMeanAbsoluteError << " / "
            << thresholds.maximumFocusMeanAbsoluteError << '\n'
            << "  severe focus-error fraction: "
            << metrics.severeFocusErrorFraction << " / "
            << thresholds.maximumSevereFocusErrorFraction << '\n'
            << "  fine-detail error: " << metrics.detailMeanAbsoluteError
            << " / " << thresholds.maximumDetailMeanAbsoluteError << '\n';
    }
}

int quality::run(const Options& opts) {
    try {
        if (!std::isfinite(opts.flow_scale) ||
                opts.flow_scale < ls::GameConfLimits::minimumFlowScale ||
                opts.flow_scale > ls::GameConfLimits::maximumFlowScale)
            throw ls::error("quality flow scale must be between 0.25 and 1.0");
        const auto kind = sceneKind(opts.scene);
        const auto scene = mako::quality::makeImageQualityRegressionScene(
            kind, opts.interpolation
        );
        const VkExtent2D extent{.width = scene.width, .height = scene.height};
        const vk::Vulkan vk = makeVulkan(opts.gpu, "mako-quality-regression");
        const std::string selectedGpu = selectedDeviceName(vk);

        std::pair<int, int> sourceFds{};
        const vk::Image previousImage{
            vk, extent, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &sourceFds.first
        };
        const vk::Image currentImage{
            vk, extent, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &sourceFds.second
        };
        int destinationFd{};
        const vk::Image destinationImage{
            vk, extent, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &destinationFd
        };

        int syncFd{};
        const vk::TimelineSemaphore sync{vk, 0, std::nullopt, &syncFd};
        const auto dll = configuredDll(opts.dll, true);
        mako::backend::Instance backend{
            [&selectedGpu](const std::string& gpuName,
                    std::pair<const std::string&, const std::string&>,
                    const std::optional<std::string>&) {
                return selectedGpu == gpuName;
            },
            dll->string(), opts.allow_fp16
        };
        mako::backend::Context& context = backend.openContext(
            sourceFds, {destinationFd}, syncFd,
            extent.width, extent.height,
            mako::backend::FrameEncoding::Sdr8,
            1.0F / opts.flow_scale, opts.performance_mode
        );

        uploadImage(vk, previousImage, scene.previous);
        sync.signal(vk, 1);
        backend.scheduleFrames(context);
        if (!sync.wait(vk, 2))
            throw ls::error("timed out while priming quality-regression history");

        uploadImage(vk, currentImage, scene.current);
        sync.signal(vk, 3);
        backend.scheduleFrames(context, std::array{opts.interpolation});
        if (!sync.wait(vk, 4))
            throw ls::error("timed out while generating the regression frame");

        const auto generated = downloadImage(
            vk,
            destinationImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            sync.handle(),
            4
        );
        const auto metrics = mako::quality::evaluateImageQuality(scene, generated);
        const auto thresholds = mako::quality::imageQualityThresholds(kind);
        const bool passed = mako::quality::passesImageQualityRegression(
            metrics, thresholds
        );

        std::cout << std::fixed << std::setprecision(5)
            << "MAKO quality result: " << (passed ? "PASS" : "FAIL")
            << " kind=frame-generation scene=" << opts.scene << '\n'
            << "  interpolation: " << opts.interpolation << '\n'
            << "  Flow Scale: " << opts.flow_scale << '\n'
            << "  model: " << (opts.performance_mode ? "performance" : "quality") << '\n'
            << "  precision: " << (opts.allow_fp16 ? "FP16 allowed" : "FP32") << '\n'
            << "  GPU: " << selectedGpu << '\n'
            << "  robustImageAccess2: "
            << (vk.supportsRobustImageAccess2() ? "enabled" : "unavailable") << '\n';
        printMetrics(metrics, thresholds);

        if (opts.output) {
            writeArtifacts(*opts.output, scene, generated);
            std::cout << "  artifacts: " << opts.output->string() << '\n';
        }

        backend.closeContext(context);
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

int quality::runSpatial(const SpatialOptions& opts) {
    try {
        const auto method = ls::scalingMethodFromName(opts.method);
        if (!method)
            throw ls::error("unknown spatial quality method: " + opts.method);
        if (!std::isfinite(opts.scaling_factor) ||
                opts.scaling_factor <= ls::GameConfLimits::minimumScalingFactor ||
                opts.scaling_factor > ls::GameConfLimits::maximumScalingFactor)
            throw ls::error("spatial quality factor must be above 1.0 and at most 2.0");
        if (!std::isfinite(opts.sharpness) ||
                opts.sharpness < ls::GameConfLimits::minimumScalingSharpness ||
                opts.sharpness > ls::GameConfLimits::maximumScalingSharpness)
            throw ls::error("spatial quality sharpness must be between 0.0 and 1.0");

        const auto kind = sceneKind(opts.scene);
        const auto scene = mako::quality::makeSpatialQualityRegressionScene(
            kind, opts.scaling_factor, opts.scene_time
        );
        const VkExtent2D sourceExtent{
            .width = scene.sourceWidth,
            .height = scene.sourceHeight,
        };
        const VkExtent2D presentationExtent{
            .width = scene.presentationWidth,
            .height = scene.presentationHeight,
        };
        const vk::Vulkan vk = makeVulkan(opts.gpu, "mako-spatial-quality-regression");
        const std::string selectedGpu = selectedDeviceName(vk);
        const auto dll = configuredDll(
            opts.dll, *method != ls::ScalingMethod::Mako
        );
        const mako::layer::SpatialScaler scaler{
            vk,
            sourceExtent,
            presentationExtent,
            VK_FORMAT_R8G8B8A8_UNORM,
            *method,
            opts.sharpness,
            dll
        };
        if (scaler.activeMethod() != *method) {
            throw ls::error(
                "requested spatial method fell back to " +
                std::string(ls::scalingMethodName(scaler.activeMethod())) +
                ": " + std::string(scaler.fallbackReason())
            );
        }

        const vk::Image applicationImage{
            vk,
            presentationExtent,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        };
        uploadSpatialSource(vk, applicationImage, sourceExtent, scene.source);

        const vk::CommandBuffer command{vk};
        command.begin(vk);
        scaler.record(
            vk,
            command,
            applicationImage.handle(),
            VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_GENERAL
        );
        command.end(vk);
        const vk::Fence scalingComplete{vk};
        command.submit(
            vk, {}, VK_NULL_HANDLE, 0,
            {}, VK_NULL_HANDLE, 0, scalingComplete.handle()
        );
        if (!scalingComplete.wait(vk))
            throw ls::error("timed out while running spatial quality regression");

        const auto generated = downloadImage(
            vk,
            applicationImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_MEMORY_READ_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        );
        const auto metrics = mako::quality::evaluateImageQuality(scene, generated);
        constexpr auto thresholds = mako::quality::spatialQualityThresholds;
        const bool passed = mako::quality::passesImageQualityRegression(
            metrics, thresholds
        );

        std::cout << std::fixed << std::setprecision(5)
            << "MAKO quality result: " << (passed ? "PASS" : "FAIL")
            << " kind=spatial-scaling scene=" << opts.scene << '\n'
            << "  method: " << ls::scalingMethodName(*method) << '\n'
            << "  factor: " << opts.scaling_factor << '\n'
            << "  sharpness: " << opts.sharpness << '\n'
            << "  scene time: " << opts.scene_time << '\n'
            << "  extent: " << sourceExtent.width << 'x' << sourceExtent.height
            << " -> " << presentationExtent.width << 'x'
            << presentationExtent.height << '\n'
            << "  GPU: " << selectedGpu << '\n';
        if (*method != ls::ScalingMethod::Mako) {
            std::cout << "  LS1 model variant: " << scaler.ls1ModelVariant() << '\n'
                << "  LS1 translator: " << scaler.ls1Translator() << '\n';
        }
        printMetrics(metrics, thresholds);

        if (opts.output) {
            writeArtifacts(*opts.output, scene, generated);
            std::cout << "  artifacts: " << opts.output->string() << '\n';
        }
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

int quality::runCombined(const CombinedOptions& opts) {
    try {
        const auto method = ls::scalingMethodFromName(opts.method);
        if (!method)
            throw ls::error("unknown combined quality spatial method: " + opts.method);
        if (!std::isfinite(opts.scaling_factor) ||
                opts.scaling_factor <= ls::GameConfLimits::minimumScalingFactor ||
                opts.scaling_factor > ls::GameConfLimits::maximumScalingFactor)
            throw ls::error("combined quality factor must be above 1.0 and at most 2.0");
        if (!std::isfinite(opts.sharpness) ||
                opts.sharpness < ls::GameConfLimits::minimumScalingSharpness ||
                opts.sharpness > ls::GameConfLimits::maximumScalingSharpness)
            throw ls::error("combined quality sharpness must be between 0.0 and 1.0");
        if (!std::isfinite(opts.flow_scale) ||
                opts.flow_scale < ls::GameConfLimits::minimumFlowScale ||
                opts.flow_scale > ls::GameConfLimits::maximumFlowScale)
            throw ls::error("combined quality flow scale must be between 0.25 and 1.0");

        const auto kind = sceneKind(opts.scene);
        const auto scene = mako::quality::makeCombinedQualityRegressionScene(
            kind, opts.scaling_factor, opts.interpolation
        );
        const VkExtent2D sourceExtent{
            .width = scene.sourceWidth,
            .height = scene.sourceHeight,
        };
        const VkExtent2D presentationExtent{
            .width = scene.presentationWidth,
            .height = scene.presentationHeight,
        };
        const vk::Vulkan vk = makeVulkan(opts.gpu, "mako-combined-quality-regression");
        const std::string selectedGpu = selectedDeviceName(vk);
        const auto dll = configuredDll(opts.dll, true);
        const mako::layer::SpatialScaler scaler{
            vk,
            sourceExtent,
            presentationExtent,
            VK_FORMAT_R8G8B8A8_UNORM,
            *method,
            opts.sharpness,
            dll
        };
        if (scaler.activeMethod() != *method) {
            throw ls::error(
                "requested combined spatial method fell back to " +
                std::string(ls::scalingMethodName(scaler.activeMethod())) +
                ": " + std::string(scaler.fallbackReason())
            );
        }

        const auto applicationUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const vk::Image previousApplication{
            vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM, applicationUsage
        };
        const vk::Image currentApplication{
            vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM, applicationUsage
        };
        std::pair<int, int> sourceFds{};
        const auto frameSourceUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;
        const vk::Image previousFrameSource{
            vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM,
            frameSourceUsage, std::nullopt, &sourceFds.first
        };
        const vk::Image currentFrameSource{
            vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM,
            frameSourceUsage, std::nullopt, &sourceFds.second
        };
        int destinationFd{};
        const vk::Image destinationImage{
            vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &destinationFd
        };
        int syncFd{};
        const vk::TimelineSemaphore sync{vk, 0, std::nullopt, &syncFd};
        mako::backend::Instance backend{
            [&selectedGpu](const std::string& gpuName,
                    std::pair<const std::string&, const std::string&>,
                    const std::optional<std::string>&) {
                return selectedGpu == gpuName;
            },
            dll->string(), opts.allow_fp16
        };
        mako::backend::Context& context = backend.openContext(
            sourceFds, {destinationFd}, syncFd,
            presentationExtent.width, presentationExtent.height,
            mako::backend::FrameEncoding::Sdr8,
            1.0F / opts.flow_scale, opts.performance_mode
        );

        submitSpatialEndpoint(
            vk,
            scaler,
            previousApplication,
            previousFrameSource,
            sourceExtent,
            scene.previous,
            sync,
            1
        );
        backend.scheduleFrames(context);
        if (!sync.wait(vk, 2))
            throw ls::error("timed out while priming combined quality history");

        submitSpatialEndpoint(
            vk,
            scaler,
            currentApplication,
            currentFrameSource,
            sourceExtent,
            scene.current,
            sync,
            3
        );
        backend.scheduleFrames(context, std::array{opts.interpolation});
        if (!sync.wait(vk, 4))
            throw ls::error("timed out while generating combined quality frame");

        const auto generated = downloadImage(
            vk,
            destinationImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            sync.handle(),
            4
        );
        const auto metrics = mako::quality::evaluateImageQuality(scene, generated);
        const auto thresholds = mako::quality::imageQualityThresholds(kind);
        const bool passed = mako::quality::passesImageQualityRegression(
            metrics, thresholds
        );

        std::cout << std::fixed << std::setprecision(5)
            << "MAKO quality result: " << (passed ? "PASS" : "FAIL")
            << " kind=combined scene=" << opts.scene << '\n'
            << "  spatial method: " << ls::scalingMethodName(*method) << '\n'
            << "  factor: " << opts.scaling_factor << '\n'
            << "  sharpness: " << opts.sharpness << '\n'
            << "  interpolation: " << opts.interpolation << '\n'
            << "  Flow Scale: " << opts.flow_scale << '\n'
            << "  model: " << (opts.performance_mode ? "performance" : "quality") << '\n'
            << "  precision: " << (opts.allow_fp16 ? "FP16 allowed" : "FP32") << '\n'
            << "  extent: " << sourceExtent.width << 'x' << sourceExtent.height
            << " -> " << presentationExtent.width << 'x'
            << presentationExtent.height << '\n'
            << "  GPU: " << selectedGpu << '\n';
        if (*method != ls::ScalingMethod::Mako) {
            std::cout << "  LS1 model variant: " << scaler.ls1ModelVariant() << '\n'
                << "  LS1 translator: " << scaler.ls1Translator() << '\n';
        }
        printMetrics(metrics, thresholds);

        if (opts.output) {
            writeArtifacts(*opts.output, scene, generated);
            std::cout << "  artifacts: " << opts.output->string() << '\n';
        }
        backend.closeContext(context);
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
