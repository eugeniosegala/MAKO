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
#include "spatial_scaling_policy.hpp"

#include <algorithm>
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
#include <numeric>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

using namespace mako::cli;
using namespace mako::cli::quality;

namespace {
    template<typename Function>
    [[nodiscard]] Function deviceFunction(
            const vk::Vulkan& vk, const char* name) {
        const auto function = reinterpret_cast<Function>(
            vk.fi().GetDeviceProcAddr(vk.dev(), name)
        );
        if (!function)
            throw ls::vulkan_error(
                "failed to get device proc addr for " + std::string(name)
            );
        return function;
    }

    class TimestampQueryPool {
    public:
        TimestampQueryPool(const vk::Vulkan& vk, const uint32_t queryCount) :
            device(vk.dev()),
            create(deviceFunction<PFN_vkCreateQueryPool>(vk, "vkCreateQueryPool")),
            destroy(deviceFunction<PFN_vkDestroyQueryPool>(vk, "vkDestroyQueryPool")),
            resetFunction(deviceFunction<PFN_vkCmdResetQueryPool>(
                vk, "vkCmdResetQueryPool"
            )),
            writeFunction(deviceFunction<PFN_vkCmdWriteTimestamp>(
                vk, "vkCmdWriteTimestamp"
            )),
            resultsFunction(deviceFunction<PFN_vkGetQueryPoolResults>(
                vk, "vkGetQueryPoolResults"
            )),
            count(queryCount) {
            uint32_t familyCount{};
            vk.fi().GetPhysicalDeviceQueueFamilyProperties(
                vk.physdev(), &familyCount, nullptr
            );
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vk.fi().GetPhysicalDeviceQueueFamilyProperties(
                vk.physdev(), &familyCount, families.data()
            );
            if (vk.queueFamilyIndex() >= families.size())
                throw ls::vulkan_error("timestamp queue family is out of range");
            this->validBits = families.at(
                vk.queueFamilyIndex()
            ).timestampValidBits;
            if (this->validBits == 0)
                throw ls::vulkan_error(
                    "selected Vulkan queue does not support timestamps"
                );
            if (this->validBits > 64)
                throw ls::vulkan_error(
                    "selected Vulkan queue reports an invalid timestamp width"
                );

            VkPhysicalDeviceProperties properties{};
            vk.fi().GetPhysicalDeviceProperties(vk.physdev(), &properties);
            this->periodNanoseconds = properties.limits.timestampPeriod;
            if (!std::isfinite(this->periodNanoseconds) ||
                    this->periodNanoseconds <= 0.0F) {
                throw ls::vulkan_error(
                    "selected Vulkan device reports an invalid timestamp period"
                );
            }

            const VkQueryPoolCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = queryCount,
            };
            const auto result = this->create(
                this->device, &info, nullptr, &this->pool
            );
            if (result != VK_SUCCESS)
                throw ls::vulkan_error(result, "vkCreateQueryPool() failed");
        }

        ~TimestampQueryPool() {
            if (this->pool != VK_NULL_HANDLE)
                this->destroy(this->device, this->pool, nullptr);
        }

        TimestampQueryPool(const TimestampQueryPool&) = delete;
        TimestampQueryPool& operator=(const TimestampQueryPool&) = delete;

        void reset(const VkCommandBuffer commandBuffer) const {
            this->resetFunction(commandBuffer, this->pool, 0, this->count);
        }

        void write(const VkCommandBuffer commandBuffer,
                const VkPipelineStageFlagBits stage,
                const uint32_t query) const {
            this->writeFunction(commandBuffer, stage, this->pool, query);
        }

        [[nodiscard]] std::vector<double> microseconds() const {
            std::vector<uint64_t> timestamps(this->count);
            const auto result = this->resultsFunction(
                this->device, this->pool, 0, this->count,
                timestamps.size() * sizeof(uint64_t), timestamps.data(),
                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT
            );
            if (result != VK_SUCCESS)
                throw ls::vulkan_error(
                    result, "vkGetQueryPoolResults() failed"
                );

            const uint64_t mask = this->validBits == 64
                ? UINT64_MAX : (uint64_t{1} << this->validBits) - 1;
            std::vector<double> samples;
            samples.reserve(this->count / 2);
            for (uint32_t query = 0; query < this->count; query += 2) {
                const uint64_t elapsed =
                    (timestamps.at(query + 1) - timestamps.at(query)) & mask;
                samples.push_back(
                    static_cast<double>(elapsed) *
                    static_cast<double>(this->periodNanoseconds) / 1000.0
                );
            }
            return samples;
        }

        [[nodiscard]] uint32_t timestampValidBits() const {
            return this->validBits;
        }

        [[nodiscard]] float timestampPeriodNanoseconds() const {
            return this->periodNanoseconds;
        }

    private:
        VkDevice device{VK_NULL_HANDLE};
        PFN_vkCreateQueryPool create{};
        PFN_vkDestroyQueryPool destroy{};
        PFN_vkCmdResetQueryPool resetFunction{};
        PFN_vkCmdWriteTimestamp writeFunction{};
        PFN_vkGetQueryPoolResults resultsFunction{};
        VkQueryPool pool{VK_NULL_HANDLE};
        uint32_t count{};
        uint32_t validBits{};
        float periodNanoseconds{};
    };

    struct ProfileStatistics {
        double minimum{};
        double median{};
        double percentile95{};
        double maximum{};
        double coefficientOfVariationPercent{};
    };

    [[nodiscard]] ProfileStatistics profileStatistics(
            const std::vector<double>& samples) {
        if (samples.empty())
            throw ls::error("spatial GPU profile returned no samples");
        std::vector<double> ordered = samples;
        std::ranges::sort(ordered);
        const size_t middle = ordered.size() / 2;
        const double median = ordered.size() % 2 == 0
            ? (ordered.at(middle - 1) + ordered.at(middle)) / 2.0
            : ordered.at(middle);
        const size_t percentile95Index = std::min(
            ordered.size() - 1,
            static_cast<size_t>(std::ceil(ordered.size() * 0.95)) - 1
        );
        const double mean = std::accumulate(
            ordered.begin(), ordered.end(), 0.0
        ) / static_cast<double>(ordered.size());
        const double squaredDeviation = std::accumulate(
            ordered.begin(), ordered.end(), 0.0,
            [mean](const double total, const double value) {
                const double difference = value - mean;
                return total + difference * difference;
            }
        );
        const double deviation = std::sqrt(
            squaredDeviation / static_cast<double>(ordered.size())
        );
        return {
            .minimum = ordered.front(),
            .median = median,
            .percentile95 = ordered.at(percentile95Index),
            .maximum = ordered.back(),
            .coefficientOfVariationPercent = mean > 0.0
                ? deviation / mean * 100.0 : 0.0,
        };
    }

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

    void initializeExternalImageLayout(const vk::Vulkan& vk,
            const vk::Image& image) {
        const vk::CommandBuffer command{vk};
        command.begin(vk);
        const auto toGeneral = imageBarrier(
            image.handle(), 0,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL
        );
        vk.df().CmdPipelineBarrier(
            command.handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
            0, nullptr, 0, nullptr, 1, &toGeneral
        );
        command.end(vk);
        command.submit(vk);
    }

    [[nodiscard]] vk::CommandBuffer submitSpatialEndpoint(const vk::Vulkan& vk,
            const mako::layer::SpatialScaler& scaler,
            const vk::Image& applicationImage,
            const vk::Image& frameGenerationSource,
            const VkExtent2D sourceExtent,
            const std::span<const uint8_t> rgba,
            const vk::TimelineSemaphore& sync,
            const uint64_t signalValue) {
        uploadSpatialSource(vk, applicationImage, sourceExtent, rgba);
        vk::CommandBuffer command{vk};
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
        return command;
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
        initializeExternalImageLayout(vk, destinationImage);

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
        if (opts.width.has_value() != opts.height.has_value())
            throw ls::error("spatial quality width and height must be provided together");
        if ((opts.width && *opts.width == 0) || (opts.height && *opts.height == 0))
            throw ls::error("spatial quality extent must be non-zero");

        const auto kind = sceneKind(opts.scene);
        const auto scene = opts.width
            ? mako::quality::makeSpatialQualityRegressionScene(
                kind,
                mako::layer::scaledSourceDimension(
                    *opts.width, opts.scaling_factor
                ),
                mako::layer::scaledSourceDimension(
                    *opts.height, opts.scaling_factor
                ),
                *opts.width,
                *opts.height,
                opts.scene_time
            )
            : mako::quality::makeSpatialQualityRegressionScene(
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
            opts.dll, ls::licensedScalingModelRequested(*method)
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
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT
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
        if (ls::licensedScalingModelRequested(*method)) {
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

int quality::runSpatialProfile(const SpatialProfileOptions& opts) {
    try {
        const auto method = ls::scalingMethodFromName(opts.method);
        if (!method)
            throw ls::error("unknown spatial profile method: " + opts.method);
        if (opts.width == 0 || opts.height == 0)
            throw ls::error("spatial profile extent must be non-zero");
        if (!std::isfinite(opts.scaling_factor) ||
                opts.scaling_factor <= ls::GameConfLimits::minimumScalingFactor ||
                opts.scaling_factor > ls::GameConfLimits::maximumScalingFactor) {
            throw ls::error(
                "spatial profile factor must be above 1.0 and at most 2.0"
            );
        }
        if (!std::isfinite(opts.sharpness) ||
                opts.sharpness < ls::GameConfLimits::minimumScalingSharpness ||
                opts.sharpness > ls::GameConfLimits::maximumScalingSharpness) {
            throw ls::error(
                "spatial profile sharpness must be between 0.0 and 1.0"
            );
        }
        if (opts.warmup_iterations == 0 || opts.warmup_iterations > 1000)
            throw ls::error(
                "spatial profile warm-up iterations must be from 1 through 1000"
            );
        if (opts.samples == 0 || opts.samples > 1000)
            throw ls::error("spatial profile samples must be from 1 through 1000");

        const VkExtent2D presentationExtent{
            .width = opts.width,
            .height = opts.height,
        };
        const VkExtent2D sourceExtent{
            .width = mako::layer::scaledSourceDimension(
                opts.width, opts.scaling_factor
            ),
            .height = mako::layer::scaledSourceDimension(
                opts.height, opts.scaling_factor
            ),
        };
        const vk::Vulkan vk = makeVulkan(opts.gpu, "mako-spatial-gpu-profile");
        const std::string selectedGpu = selectedDeviceName(vk);
        const auto dll = configuredDll(
            opts.dll, ls::licensedScalingModelRequested(*method)
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
                "requested spatial profile method fell back to " +
                std::string(ls::scalingMethodName(scaler.activeMethod())) +
                ": " + std::string(scaler.fallbackReason())
            );
        }

        const auto applicationUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        const vk::Image applicationImage{
            vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM,
            applicationUsage
        };
        std::optional<vk::Image> frameGenerationSource;
        if (opts.frame_generation_handoff) {
            frameGenerationSource.emplace(
                vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            );
        }
        const size_t sourceBytes = static_cast<size_t>(sourceExtent.width) *
            sourceExtent.height * 4;
        const std::vector<uint8_t> sourcePixels(sourceBytes, 0x80);
        uploadSpatialSource(
            vk, applicationImage, sourceExtent, sourcePixels
        );

        const auto recordScaler = [&](const vk::CommandBuffer& command) {
            scaler.record(
                vk,
                command,
                applicationImage.handle(),
                frameGenerationSource
                    ? frameGenerationSource->handle() : VK_NULL_HANDLE,
                VK_IMAGE_LAYOUT_GENERAL
            );
        };
        {
            const vk::CommandBuffer warmup{vk};
            warmup.begin(vk);
            for (uint32_t iteration = 0;
                    iteration < opts.warmup_iterations; ++iteration) {
                recordScaler(warmup);
            }
            warmup.end(vk);
            warmup.submit(vk);
        }

        TimestampQueryPool timestamps(vk, opts.samples * 2);
        const vk::CommandBuffer measured{vk};
        measured.begin(vk);
        timestamps.reset(measured.handle());
        for (uint32_t sample = 0; sample < opts.samples; ++sample) {
            timestamps.write(
                measured.handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                sample * 2
            );
            recordScaler(measured);
            timestamps.write(
                measured.handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                sample * 2 + 1
            );
        }
        measured.end(vk);
        measured.submit(vk);

        const auto samples = timestamps.microseconds();
        const auto statistics = profileStatistics(samples);
        std::cout << std::fixed << std::setprecision(3)
            << "MAKO spatial GPU profile: PASS schema=1\n"
            << "  method: " << ls::scalingMethodName(*method) << '\n'
            << "  factor: " << opts.scaling_factor << '\n'
            << "  sharpness: " << opts.sharpness << '\n'
            << "  frame-generation handoff: "
            << (opts.frame_generation_handoff ? "yes" : "no") << '\n'
            << "  extent: " << sourceExtent.width << 'x' << sourceExtent.height
            << " -> " << presentationExtent.width << 'x'
            << presentationExtent.height << '\n'
            << "  GPU: " << selectedGpu << '\n'
            << "  timestamp valid bits: "
            << timestamps.timestampValidBits() << '\n'
            << "  timestamp period ns: "
            << timestamps.timestampPeriodNanoseconds() << '\n'
            << "  warm-up iterations: " << opts.warmup_iterations << '\n'
            << "  samples: " << opts.samples << '\n'
            << "  gpu-time minimum us: " << statistics.minimum << '\n'
            << "  gpu-time median us: " << statistics.median << '\n'
            << "  gpu-time p95 us: " << statistics.percentile95 << '\n'
            << "  gpu-time maximum us: " << statistics.maximum << '\n'
            << "  gpu-time cv percent: "
            << statistics.coefficientOfVariationPercent << '\n'
            << "  sample-us:";
        for (const double sample : samples)
            std::cout << ' ' << sample;
        std::cout << '\n';
        if (ls::licensedScalingModelRequested(*method)) {
            std::cout << "  LS1 model variant: " << scaler.ls1ModelVariant() << '\n'
                << "  LS1 translator: " << scaler.ls1Translator() << '\n';
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

int quality::runSynchronizationCanary(
        const SynchronizationCanaryOptions& opts) {
    try {
        const vk::Vulkan vk = makeVulkan(
            opts.gpu, "mako-synchronization-validation-canary"
        );
        constexpr std::array<uint32_t, 16> initial{};
        const auto usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        const vk::Buffer source{vk, initial, usage};
        const vk::Buffer destination{vk, initial, usage};
        const auto fill = deviceFunction<PFN_vkCmdFillBuffer>(
            vk, "vkCmdFillBuffer"
        );
        const auto copy = deviceFunction<PFN_vkCmdCopyBuffer>(
            vk, "vkCmdCopyBuffer"
        );
        const VkBufferCopy region{.size = sizeof(initial)};
        const vk::CommandBuffer command{vk};
        command.begin(vk);
        fill(
            command.handle(), source.handle(), 0, sizeof(initial), 0x5a5a5a5a
        );
        copy(
            command.handle(), source.handle(), destination.handle(), 1, &region
        );
        command.end(vk);
        std::cout << "MAKO synchronization-validation canary: RECORDED\n";
        return EXIT_SUCCESS;
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
        if (opts.width.has_value() != opts.height.has_value())
            throw ls::error("combined quality width and height must be provided together");
        if ((opts.width && *opts.width == 0) || (opts.height && *opts.height == 0))
            throw ls::error("combined quality extent must be non-zero");

        const auto kind = sceneKind(opts.scene);
        const auto scene = opts.width
            ? mako::quality::makeCombinedQualityRegressionScene(
                kind,
                mako::layer::scaledSourceDimension(
                    *opts.width, opts.scaling_factor
                ),
                mako::layer::scaledSourceDimension(
                    *opts.height, opts.scaling_factor
                ),
                *opts.width,
                *opts.height,
                opts.interpolation
            )
            : mako::quality::makeCombinedQualityRegressionScene(
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
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        initializeExternalImageLayout(vk, destinationImage);
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

        const auto previousSpatialCommand = submitSpatialEndpoint(
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

        const auto currentSpatialCommand = submitSpatialEndpoint(
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
        if (ls::licensedScalingModelRequested(*method)) {
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
