/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "quality.hpp"
#include "quality_inputs.hpp"
#include "benchmark_input.hpp"
#include "profile_statistics.hpp"
#include "mako-common/vulkan/timestamp_query_pool.hpp"
#include "image_transfer.hpp"
#include "mako-common/vulkan/external_image.hpp"
#include "temporal_sequence.hpp"
#include "mako-backend/mako.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/file_descriptors.hpp"
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
#include <chrono>
#include <thread>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
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
    using mako::cli::images::imageBarrier;
    using mako::cli::images::uploadImage;
    using vk::deviceFunction;
    using vk::TimestampQueryPool;

    using mako::cli::profileStatistics;

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

    void uploadSpatialSource(const vk::Vulkan& vk, const vk::Image& image,
            const VkExtent2D sourceExtent, const std::span<const uint8_t> rgba,
            const size_t bytesPerPixel = 4) {
        const size_t expectedBytes = static_cast<size_t>(sourceExtent.width) *
            sourceExtent.height * bytesPerPixel;
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
            const uint64_t waitValue = 0, const bool restoreGeneral = false,
            const size_t bytesPerPixel = 4) {
        const size_t byteCount = static_cast<size_t>(image.getExtent().width) *
            image.getExtent().height * bytesPerPixel;
        const std::vector<uint8_t> zeroes(byteCount);
        const vk::Buffer staging{
            vk, zeroes.data(), zeroes.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT
        };
        const vk::CommandBuffer command{vk};
        command.begin(vk);
        if (image.external())
            vk::acquireExternalImage(vk, command.handle(), image.handle(),
                VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
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
        if (restoreGeneral) {
            const auto toGeneral = imageBarrier(image.handle(), VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            vk.df().CmdPipelineBarrier(command.handle(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGeneral);
        }
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
    int runTemporal(const Options& opts) {
        const auto plan = parseSequence(*opts.sequence_plan);
        if (plan.size() < 12)
            throw ls::error("temporal quality requires at least 12 planned source frames");
        size_t capacity = 0;
        for (const auto& frame : plan)
            capacity = std::max(capacity, frame.size());
        if (capacity == 0)
            throw ls::error("temporal quality needs at least one generated output");
        const auto kind = sceneKind(opts.scene);
        if (opts.width.has_value() != opts.height.has_value())
            throw ls::error("temporal quality extents must be provided together");
        const auto initial = mako::quality::makeImageQualityRegressionScene(
            kind, opts.width.value_or(321), opts.height.value_or(181), 0, 1, 0.5F);
        const VkExtent2D extent{initial.width, initial.height};
        const vk::Vulkan vk = makeVulkan(opts.gpu, "mako-temporal-quality");
        const auto gpu = selectedDeviceName(vk);
        std::array<int, 2> sourceFds{-1, -1};
        ls::FileDescriptorScope sourceScope{sourceFds};
        const std::array<vk::Image, 2> sources{
            vk::Image(vk, extent, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt, &sourceFds[0]),
            vk::Image(vk, extent, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt, &sourceFds[1])};
        std::vector<int> destinationFds(capacity, -1);
        ls::FileDescriptorScope destinationScope{destinationFds};
        std::vector<vk::Image> destinations;
        destinations.reserve(capacity);
        for (auto& fd : destinationFds)
            destinations.emplace_back(vk, extent, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt, &fd);
        for (const auto& destination : destinations)
            initializeExternalImageLayout(vk, destination);
        int syncFd{-1};
        ls::FileDescriptorScope syncScope{{&syncFd, 1}};
        const vk::TimelineSemaphore sync{vk, 0, std::nullopt, &syncFd};
        const auto dll = configuredDll(opts.dll, true);
        mako::backend::Instance backend{
            [&gpu](const std::string& name,
                    std::pair<const std::string&, const std::string&>,
                    const std::optional<std::string>&) { return name == gpu; },
            *dll, opts.allow_fp16, ls::makoFgPackageOverride()};
        auto& context = backend.openContext(
            (sourceScope.release(), std::pair{sourceFds[0], sourceFds[1]}),
            (destinationScope.release(), destinationFds),
            (syncScope.release(), syncFd), extent.width, extent.height,
            mako::backend::FrameEncoding::Sdr8,
            1.0F / opts.flow_scale, opts.performance_mode, false, 1);
        const auto awaitContext = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!backend.contextReady(context)) {
                if (std::chrono::steady_clock::now() >= deadline)
                    throw ls::error("temporal quality context completion timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        uploadImage(vk, sources[0], initial.previous);
        uploadImage(vk, sources[1], initial.previous);
        uint64_t timeline = 1;
        sync.signal(vk, timeline++);
        backend.scheduleFrameHistory(context);
        awaitContext();
        std::ofstream rows;
        if (opts.output) {
            std::filesystem::create_directories(*opts.output);
            rows.open(*opts.output / "sequence.tsv");
            if (!rows) throw ls::error("cannot write temporal quality summary");
            std::filesystem::create_directories(*opts.output / "sources");
            writePpm(*opts.output / "sources" / "0.ppm", extent.width, extent.height, initial.previous);
            rows << "frame\toutput\tinterpolation\tprevious_time\tcurrent_time\tmae\tfocus_mae\tsevere_fraction\tdetail_mae\tresult\n";
        }
        bool passed = true;
        size_t generatedCount = 0;
        size_t historyCount = 1;
        for (size_t index = 0; index < plan.size(); ++index) {
            const size_t frame = index + 1;
            const auto previousTime = sequenceSceneTime(frame - 1);
            const auto currentTime = sequenceSceneTime(frame);
            auto scene = mako::quality::makeImageQualityRegressionScene(
                kind, extent.width, extent.height, previousTime, currentTime, 0.5F);
            uploadImage(vk, sources.at(frame % 2), scene.current);
            if (opts.output)
                writePpm(*opts.output / "sources" / (std::to_string(frame) + ".ppm"),
                    extent.width, extent.height, scene.current);
            sync.signal(vk, timeline++);
            const auto& timestamps = plan.at(index);
            if (timestamps.empty()) {
                backend.scheduleFrameHistory(context);
                awaitContext();
                ++historyCount;
                if (rows) rows << frame << "\t-1\t0\t" << previousTime << '\t'
                    << currentTime << "\t0\t0\t0\t0\tHISTORY\n";
                continue;
            }
            backend.scheduleFrames(context, timestamps);
            timeline += timestamps.size();
            awaitContext();
            for (size_t output = 0; output < timestamps.size(); ++output) {
                scene = mako::quality::makeImageQualityRegressionScene(
                    kind, extent.width, extent.height, previousTime, currentTime,
                    timestamps.at(output));
                const auto generated = downloadImage(vk, destinations.at(output),
                    VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    sync.handle(), timeline - timestamps.size() + output, true);
                const auto metrics = mako::quality::evaluateImageQuality(scene, generated);
                const bool good = mako::quality::passesImageQualityRegression(
                    metrics, mako::quality::imageQualityThresholds(kind));
                passed = passed && good;
                ++generatedCount;
                if (opts.output) {
                    const auto directory = *opts.output /
                        ("frame-" + std::to_string(frame) + "-output-" + std::to_string(output));
                    writeArtifacts(directory, scene, generated);
                    rows << std::setprecision(9) << frame << '\t' << output << '\t'
                        << timestamps.at(output) << '\t' << previousTime << '\t' << currentTime << '\t'
                        << metrics.meanAbsoluteError << '\t' << metrics.focusMeanAbsoluteError << '\t'
                        << metrics.severeFocusErrorFraction << '\t' << metrics.detailMeanAbsoluteError
                        << '\t' << (good ? "PASS" : "FAIL") << '\n';
                }
            }
        }
        if (rows.is_open()) {
            rows.flush();
            if (!rows) throw ls::error("temporal quality summary write failed");
        }
        std::cout << "MAKO_TEMPORAL_QUALITY recipe=1 result=" << (passed ? "PASS" : "FAIL")
                  << " source_frames=" << plan.size() + 1 << " generated_frames=" << generatedCount
                  << " history_frames=" << historyCount << " capacity=" << capacity
                  << " scene=" << opts.scene << " scene_clock=triangle24-v1\n";
        backend.closeContext(context);
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}

int quality::run(const Options& opts) {
    try {
        if (!std::isfinite(opts.flow_scale) ||
                opts.flow_scale < ls::GameConfLimits::minimumFlowScale ||
                opts.flow_scale > ls::GameConfLimits::maximumFlowScale)
            throw ls::error("quality flow scale must be between 0.25 and 1.0");
        if ((opts.hdr_reduced_precision && !opts.hdr10) ||
                (opts.hdr10 && opts.sequence_plan) || !std::isfinite(opts.hdr_white_nits) ||
                opts.hdr_white_nits <= 0 || opts.hdr_white_nits > 10000)
            throw ls::error("HDR quality requires explicit HDR10, valid reference white and a single scene");
        if (opts.input_pair && (opts.sequence_plan || opts.hdr10 || !opts.output ||
                opts.scene != "motion-boundary"))
            throw ls::error("input pair requires SDR, a new output directory and no procedural scene or sequence");
        if (opts.input_pair && (!std::isfinite(opts.interpolation) ||
                opts.interpolation <= 0 || opts.interpolation >= 1))
            throw ls::error("input pair interpolation must be strictly between zero and one");
        if (opts.sequence_plan) return runTemporal(opts);
        if (opts.width || opts.height)
            throw ls::error("quality extents require --sequence-plan");
        const auto kind = sceneKind(opts.scene);
        const auto scene = opts.input_pair ? readQualityPair(*opts.input_pair) :
            mako::quality::makeImageQualityRegressionScene(kind, opts.interpolation);
        if (opts.input_pair && !std::filesystem::create_directory(*opts.output))
            throw ls::error("input pair output already exists; choose a fresh directory");
        const VkExtent2D extent{.width = scene.width, .height = scene.height};
        const vk::Vulkan vk = makeVulkan(opts.gpu, "mako-quality-regression");
        const std::string selectedGpu = selectedDeviceName(vk);

        if (opts.hdr_encoding != "packed" && opts.hdr_encoding != "pq16" && opts.hdr_encoding != "scrgb")
            throw ls::error("HDR encoding must be packed, pq16 or scrgb");
        const bool halfFloat = opts.hdr10 && opts.hdr_encoding != "packed", pq16 = opts.hdr_encoding == "pq16";
        const auto imageFormat = !opts.hdr10 ? VK_FORMAT_R8G8B8A8_UNORM
            : halfFloat ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        const auto encoding = !opts.hdr10 ? mako::backend::FrameEncoding::Sdr8
            : !halfFloat ? mako::backend::FrameEncoding::Hdr10PqPacked
            : pq16 ? mako::backend::FrameEncoding::Hdr10Pq : mako::backend::FrameEncoding::ScRgbLinear;
        const auto input = [&](const std::vector<uint8_t>& rgba) {
            return !opts.hdr10 ? rgba : halfFloat ? benchmark::hdr16Input(rgba, opts.hdr_white_nits, pq16)
                : benchmark::hdr10ProfileInput(rgba, opts.hdr_white_nits);
        };
        std::array<int, 2> sourceFds{-1, -1};
        ls::FileDescriptorScope sourceScope{sourceFds};
        const vk::Image previousImage{
            vk, extent, imageFormat,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &sourceFds[0]
        };
        const vk::Image currentImage{
            vk, extent, imageFormat,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &sourceFds[1]
        };
        std::vector<int> destinationFds(1, -1);
        ls::FileDescriptorScope destinationScope{destinationFds};
        const vk::Image destinationImage{
            vk, extent, imageFormat,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &destinationFds[0]
        };
        initializeExternalImageLayout(vk, destinationImage);

        int syncFd{-1};
        ls::FileDescriptorScope syncScope{{&syncFd, 1}};
        const vk::TimelineSemaphore sync{vk, 0, std::nullopt, &syncFd};
        const auto dll = configuredDll(opts.dll, true);
        mako::backend::Instance backend{
            [&selectedGpu](const std::string& gpuName,
                    std::pair<const std::string&, const std::string&>,
                    const std::optional<std::string>&) {
                return selectedGpu == gpuName;
            },
            dll->string(), opts.allow_fp16, ls::makoFgPackageOverride()
        };
        if (opts.hdr10 && !halfFloat && !backend.supportsPackedHdr10Transport())
            throw ls::error("Packed HDR10 quality is unsupported on this device");
        mako::backend::Context& context = backend.openContext(
            (sourceScope.release(), std::pair{sourceFds[0], sourceFds[1]}),
            (destinationScope.release(), destinationFds),
            (syncScope.release(), syncFd),
            extent.width, extent.height,
            encoding, 1.0F / opts.flow_scale, opts.performance_mode, opts.hdr_reduced_precision, 1
        );

        uploadImage(vk, previousImage, input(scene.previous));
        uploadImage(vk, currentImage, input(scene.previous));
        sync.signal(vk, 1);
        backend.scheduleFrames(context);
        if (!sync.wait(vk, 2))
            throw ls::error("timed out while priming quality-regression history");

        uploadImage(vk, currentImage, input(scene.current));
        sync.signal(vk, 3);
        backend.scheduleFrames(context, std::array{opts.interpolation});
        if (!sync.wait(vk, 4))
            throw ls::error("timed out while generating the regression frame");

        const auto readback = downloadImage(
            vk,
            destinationImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            sync.handle(),
            4, false, halfFloat ? 8 : 4
        );
        const auto generated = !opts.hdr10 ? readback : halfFloat
            ? benchmark::hdr16Preview(readback, opts.hdr_white_nits, pq16)
            : benchmark::hdr10QualityPreview(readback, opts.hdr_white_nits);
        if (opts.hdr10)
            std::cout << "MAKO_HDR_QUALITY encoding=" << (!halfFloat ? "hdr10-pq-packed" : pq16 ? "hdr10-pq" : "scrgb-linear") << " reduced="
                << opts.hdr_reduced_precision << " white_nits=" << opts.hdr_white_nits << '\n';
        const auto metrics = mako::quality::evaluateImageQuality(scene, generated);
        if (opts.input_pair) {
            writeArtifacts(*opts.output, scene, generated);
            std::cout << std::setprecision(9)
                << "MAKO Renderer: image-pair result=OBSERVED width=" << scene.width
                << " height=" << scene.height << " interpolation=" << opts.interpolation
                << " flow=" << opts.flow_scale << " model="
                << (opts.performance_mode ? "performance" : "quality")
                << " precision_requested=" << (opts.allow_fp16 ? "fp16" : "fp32")
                << " mae=" << metrics.meanAbsoluteError << '\n';
            backend.closeContext(context);
            return EXIT_SUCCESS;
        }
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
        mako::layer::SpatialScaler scaler{
            vk,
            sourceExtent,
            presentationExtent,
            VK_FORMAT_R8G8B8A8_UNORM,
            *method,
            opts.sharpness,
            dll, opts.allow_fp16
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
            << "  spatial precision: " << scaler.precisionName() << '\n'
            << "  factor: " << opts.scaling_factor << '\n'
            << "  sharpness: " << opts.sharpness << '\n'
            << "  scene time: " << opts.scene_time << '\n'
            << "  extent: " << sourceExtent.width << 'x' << sourceExtent.height
            << " -> " << presentationExtent.width << 'x'
            << presentationExtent.height << '\n'
            << "  GPU: " << selectedGpu << '\n';
        if (ls::licensedScalingModelRequested(*method)) {
            std::cout << "  LS1 model variant: " << scaler.ls1ModelVariant() << '\n'
                << "  LS1 translator: " << scaler.ls1Translator() << '\n'
                << "  DLL SHA-256: " << scaler.ls1DllSha256() << '\n'
                << "  resource-layout SHA-256: "
                << scaler.ls1ResourceLayoutSha256() << '\n';
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

namespace {
    /// A profiled presentation format and the working image and encoding the
    /// scaler uses for it, matching the Renderer's colour classification.
    struct SpatialPresentationFormat {
        VkFormat format;
        VkFormat working;
        size_t bytesPerPixel;
        mako::backend::FrameEncoding encoding;
    };

    [[nodiscard]] SpatialPresentationFormat spatialPresentationFormat(
            const std::string_view name) {
        using mako::backend::FrameEncoding;
        if (name == "rgba8")
            return {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, 4, FrameEncoding::Sdr8};
        if (name == "bgra8")
            return {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, 4, FrameEncoding::Sdr8};
        if (name == "a2b10g10r10")
            return {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_R16G16B16A16_SFLOAT, 4,
                FrameEncoding::SdrHighPrecision};
        if (name == "a2r10g10b10")
            return {VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_FORMAT_R16G16B16A16_SFLOAT, 4,
                FrameEncoding::SdrHighPrecision};
        if (name == "rgba16f")
            return {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, 8,
                FrameEncoding::SdrHighPrecision};
        if (name == "scrgb")
            return {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, 8,
                FrameEncoding::ScRgbLinear};
        throw ls::error("unknown spatial profile presentation format: " + std::string(name));
    }

    /// Half-float bits for a value in [0, 1], rounded to nearest.
    [[nodiscard]] uint16_t unitHalf(const float value) {
        if (value <= 0.0F)
            return 0;
        int exponent{};
        const float mantissa = std::frexp(value, &exponent);
        const auto fraction = static_cast<uint32_t>(
            std::lround((mantissa * 2.0F - 1.0F) * 1024.0F));
        const auto halfExponent = static_cast<uint32_t>(exponent + 14);
        return static_cast<uint16_t>(fraction == 1024
            ? (halfExponent + 1) << 10 : (halfExponent << 10) | fraction);
    }

    /// Deterministic high-contrast source bytes: hashed texels on a checker
    /// of smooth ramps, opaque alpha. Half-float images receive unit values.
    [[nodiscard]] std::vector<uint8_t> spatialComparisonSource(
            const VkExtent2D extent, const SpatialPresentationFormat& format) {
        std::vector<uint8_t> bytes(static_cast<size_t>(extent.width) *
            extent.height * format.bytesPerPixel);
        for (uint32_t y = 0; y < extent.height; ++y) {
            for (uint32_t x = 0; x < extent.width; ++x) {
                const size_t pixel = static_cast<size_t>(y) * extent.width + x;
                std::array<uint8_t, 4> rgba{};
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    uint32_t hash = x * 73856093U ^ y * 19349663U ^ channel * 83492791U;
                    hash ^= hash >> 13;
                    hash *= 0x5bd1e995U;
                    hash ^= hash >> 15;
                    const bool textured = ((x / 7) + (y / 5)) % 2 == 0;
                    rgba.at(channel) = channel == 3 ? 255 : static_cast<uint8_t>(
                        textured ? hash & 0xffU : (x * 3 + y + channel * 40) & 0xffU);
                }
                if (format.bytesPerPixel == 8) {
                    for (uint32_t channel = 0; channel < 4; ++channel) {
                        const auto half = unitHalf(rgba.at(channel) / 255.0F);
                        bytes.at(pixel * 8 + channel * 2) = static_cast<uint8_t>(half & 0xffU);
                        bytes.at(pixel * 8 + channel * 2 + 1) = static_cast<uint8_t>(half >> 8);
                    }
                } else {
                    std::copy(rgba.begin(), rgba.end(), bytes.begin() +
                        static_cast<std::ptrdiff_t>(pixel * 4));
                }
            }
        }
        return bytes;
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
        if (opts.warmup_milliseconds > 60000)
            throw ls::error(
                "spatial profile warm-up time must be at most 60000 ms"
            );
        if (opts.samples == 0 || opts.samples > 1000)
            throw ls::error("spatial profile samples must be from 1 through 1000");
        const auto presentationFormat =
            spatialPresentationFormat(opts.presentation_format);
        const bool directRequested =
            opts.direct_output || opts.compare_direct_output;
        if (directRequested && opts.frame_generation_handoff) {
            throw ls::error(
                "direct spatial output requires no frame-generation handoff"
            );
        }
        const bool nativeBlit = *method == ls::ScalingMethod::Native;

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
        mako::layer::SpatialScaler scaler{
            vk,
            sourceExtent,
            presentationExtent,
            presentationFormat.working,
            *method,
            opts.sharpness,
            dll, opts.allow_fp16, presentationFormat.encoding
        };
        if (scaler.activeMethod() != *method) {
            throw ls::error(
                "requested spatial profile method fell back to " +
                std::string(ls::scalingMethodName(scaler.activeMethod())) +
                ": " + std::string(scaler.fallbackReason())
            );
        }

        // The application image stands in for a swapchain image, which is
        // always a colour attachment; that usage also lets the driver
        // compress it, which direct output must preserve. Native Resolution
        // blits and, like MAKO's display resize, keeps transfer-only usage.
        const auto applicationUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            (directRequested && !nativeBlit ? VK_IMAGE_USAGE_STORAGE_BIT : 0U);
        const vk::Image applicationImage{
            vk, presentationExtent, presentationFormat.format,
            applicationUsage
        };
        if (opts.direct_output) {
            const std::array images{applicationImage.handle()};
            if (scaler.configureDirectPresentationOutputs(
                    vk, images, presentationFormat.format) != 1) {
                throw ls::error(
                    "direct spatial output is unavailable for this format"
                );
            }
        }
        std::optional<vk::Image> frameGenerationSource;
        if (opts.frame_generation_handoff) {
            frameGenerationSource.emplace(
                vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT
            );
            const std::array directOutputs{
                std::cref(*frameGenerationSource)
            };
            scaler.configureDirectFrameGenerationOutputs(
                vk, directOutputs
            );
        }
        const size_t sourceBytes = static_cast<size_t>(sourceExtent.width) *
            sourceExtent.height * presentationFormat.bytesPerPixel;
        std::vector<uint8_t> sourcePixels(sourceBytes, 0x80);
        if (presentationFormat.bytesPerPixel == 8) {
            for (size_t i = 0; i + 1 < sourcePixels.size(); i += 2) {
                sourcePixels.at(i) = 0x00;
                sourcePixels.at(i + 1) = 0x38;  // 0.5 in half precision
            }
        }
        uploadSpatialSource(
            vk, applicationImage, sourceExtent, sourcePixels,
            presentationFormat.bytesPerPixel
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
        // Automatic GPU clock management ramps with sustained load, so a
        // fixed iteration count leaves cheap graphs timed at a lower clock.
        const auto warmupStart = std::chrono::steady_clock::now();
        const std::chrono::milliseconds warmupTarget{opts.warmup_milliseconds};
        uint64_t warmupIterations = 0;
        do {
            const vk::CommandBuffer warmup{vk};
            warmup.begin(vk);
            for (uint32_t iteration = 0;
                    iteration < opts.warmup_iterations; ++iteration) {
                recordScaler(warmup);
            }
            warmup.end(vk);
            warmup.submit(vk);
            warmupIterations += opts.warmup_iterations;
        } while (std::chrono::steady_clock::now() - warmupStart < warmupTarget);
        const auto warmupElapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - warmupStart);

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
            << "MAKO spatial GPU profile: PASS schema=2\n"
            << "  method: " << ls::scalingMethodName(*method) << '\n'
            << "  spatial precision: " << scaler.precisionName() << '\n'
            << "  factor: " << opts.scaling_factor << '\n'
            << "  sharpness: " << opts.sharpness << '\n'
            << "  frame-generation handoff: "
            << (opts.frame_generation_handoff ? "yes" : "no") << '\n'
            << "  presentation format: " << opts.presentation_format << '\n'
            << "  presentation transport: "
            << (scaler.directPresentationOutputCount() == 0
                    ? "copy-private-output"
                    : nativeBlit ? "direct-blit" : "direct-reconstruction")
            << '\n'
            << "  frame-generation transport: "
            << (!opts.frame_generation_handoff
                    ? "none"
                    : scaler.directFrameGenerationOutputCount() > 0
                        ? "direct-reconstruction"
                        : "copy-private-output")
            << '\n'
            << "  extent: " << sourceExtent.width << 'x' << sourceExtent.height
            << " -> " << presentationExtent.width << 'x'
            << presentationExtent.height << '\n'
            << "  GPU: " << selectedGpu << '\n'
            << "  timestamp valid bits: "
            << timestamps.timestampValidBits() << '\n'
            << "  timestamp period ns: "
            << timestamps.timestampPeriodNanoseconds() << '\n'
            << "  warm-up iterations: " << warmupIterations << '\n'
            << "  warm-up ms: " << warmupElapsed.count() << '\n'
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
        if (opts.compare_direct_output) {
            // The same scene through both paths: copy-back into one image,
            // direct reconstruction into another with identical properties.
            const vk::Image copied{vk, presentationExtent,
                presentationFormat.format, applicationUsage};
            const vk::Image direct{vk, presentationExtent,
                presentationFormat.format, applicationUsage};
            const std::array targets{direct.handle()};
            if (scaler.configureDirectPresentationOutputs(
                    vk, targets, presentationFormat.format) != 1) {
                throw ls::error(
                    "direct spatial output is unavailable for this format"
                );
            }
            const auto scene = spatialComparisonSource(
                sourceExtent, presentationFormat);
            const auto reconstruct = [&](const vk::Image& image) {
                uploadSpatialSource(vk, image, sourceExtent, scene,
                    presentationFormat.bytesPerPixel);
                const vk::CommandBuffer command{vk};
                command.begin(vk);
                scaler.record(vk, command, image.handle(), VK_NULL_HANDLE,
                    VK_IMAGE_LAYOUT_GENERAL);
                command.end(vk);
                command.submit(vk);
                return downloadImage(vk, image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_NULL_HANDLE, 0, false, presentationFormat.bytesPerPixel);
            };
            const auto copiedPixels = reconstruct(copied);
            const auto directPixels = reconstruct(direct);
            size_t differing = 0;
            for (size_t i = 0; i < copiedPixels.size(); ++i)
                differing += copiedPixels.at(i) != directPixels.at(i);
            std::cout << "  direct output identical: "
                << (differing == 0 ? "yes" : "no")
                << "; differing bytes: " << differing << " of "
                << copiedPixels.size() << '\n';
            if (differing != 0)
                return EXIT_FAILURE;
        }
        if (ls::licensedScalingModelRequested(*method)) {
            std::cout << "  LS1 model variant: " << scaler.ls1ModelVariant() << '\n'
                << "  LS1 translator: " << scaler.ls1Translator() << '\n'
                << "  DLL SHA-256: " << scaler.ls1DllSha256() << '\n'
                << "  resource-layout SHA-256: "
                << scaler.ls1ResourceLayoutSha256() << '\n';
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
        mako::layer::SpatialScaler scaler{
            vk,
            sourceExtent,
            presentationExtent,
            VK_FORMAT_R8G8B8A8_UNORM,
            *method,
            opts.sharpness,
            dll, opts.allow_fp16
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
        std::array<int, 2> sourceFds{-1, -1};
        ls::FileDescriptorScope sourceScope{sourceFds};
        const auto frameSourceUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT;
        const vk::Image previousFrameSource{
            vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM,
            frameSourceUsage, std::nullopt, &sourceFds[0]
        };
        const vk::Image currentFrameSource{
            vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM,
            frameSourceUsage, std::nullopt, &sourceFds[1]
        };
        const std::array directOutputs{
            std::cref(previousFrameSource),
            std::cref(currentFrameSource),
        };
        scaler.configureDirectFrameGenerationOutputs(vk, directOutputs);
        std::vector<int> destinationFds(1, -1);
        ls::FileDescriptorScope destinationScope{destinationFds};
        const vk::Image destinationImage{
            vk, presentationExtent, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &destinationFds[0]
        };
        initializeExternalImageLayout(vk, destinationImage);
        int syncFd{-1};
        ls::FileDescriptorScope syncScope{{&syncFd, 1}};
        const vk::TimelineSemaphore sync{vk, 0, std::nullopt, &syncFd};
        mako::backend::Instance backend{
            [&selectedGpu](const std::string& gpuName,
                    std::pair<const std::string&, const std::string&>,
                    const std::optional<std::string>&) {
                return selectedGpu == gpuName;
            },
            dll->string(), opts.allow_fp16, ls::makoFgPackageOverride()
        };
        mako::backend::Context& context = backend.openContext(
            (sourceScope.release(), std::pair{sourceFds[0], sourceFds[1]}),
            (destinationScope.release(), destinationFds),
            (syncScope.release(), syncFd),
            presentationExtent.width, presentationExtent.height,
            mako::backend::FrameEncoding::Sdr8,
            1.0F / opts.flow_scale, opts.performance_mode, false, 1
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
            << "  spatial FG transport: "
            << (scaler.directFrameGenerationOutputCount() == 2
                    ? "direct-reconstruction" : "copy-private-output")
            << '\n'
            << "  spatial precision: " << scaler.precisionName() << '\n'
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
                << "  LS1 translator: " << scaler.ls1Translator() << '\n'
                << "  DLL SHA-256: " << scaler.ls1DllSha256() << '\n'
                << "  resource-layout SHA-256: "
                << scaler.ls1ResourceLayoutSha256() << '\n';
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
