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

#include <array>
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
    VkPhysicalDevice selectDevice(const vk::VulkanInstanceFuncs& functions,
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

    void uploadImage(const vk::Vulkan& vk, const vk::Image& image,
            const std::span<const uint8_t> rgba) {
        const vk::Buffer staging{
            vk, rgba.data(), rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        };
        const vk::CommandBuffer command{vk};
        command.begin(vk);
        command.copyBufferToImage(vk, staging, image);
        command.end(vk);
        command.submit(vk);
    }

    std::vector<uint8_t> downloadImage(const vk::Vulkan& vk,
            const vk::Image& image, const vk::TimelineSemaphore& sync,
            const uint64_t waitValue) {
        const size_t byteCount = static_cast<size_t>(image.getExtent().width) *
            image.getExtent().height * 4;
        const std::vector<uint8_t> zeroes(byteCount);
        const vk::Buffer staging{
            vk, zeroes.data(), zeroes.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT
        };
        const vk::CommandBuffer command{vk};
        command.begin(vk);
        command.copyImageToBuffer(vk, image, staging);
        command.end(vk);
        const vk::Fence completion{vk};
        command.submit(vk,
            {}, sync.handle(), waitValue,
            {}, VK_NULL_HANDLE, 0,
            completion.handle()
        );
        if (!completion.wait(vk))
            throw ls::error("timed out while reading the generated image");
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
}

int quality::run(const Options& opts) {
    try {
        constexpr auto preset = mako::quality::flowScaleOnePreset;
        const auto scene = mako::quality::makeAmdImageQualityRegressionScene();
        const VkExtent2D extent{.width = scene.width, .height = scene.height};

        const vk::Vulkan vk{
            "mako-quality-regression", vk::version{2, 0, 0},
            "mako-quality-regression", vk::version{2, 0, 0},
            [&opts](const vk::VulkanInstanceFuncs& functions,
                    const std::vector<VkPhysicalDevice>& devices) {
                return selectDevice(functions, devices, opts.gpu);
            }
        };
        VkPhysicalDeviceProperties2 selectedProperties{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        };
        vk.fi().GetPhysicalDeviceProperties2(vk.physdev(), &selectedProperties);
        const std::array<char, 256> selectedName = std::to_array(
            selectedProperties.properties.deviceName
        );
        const std::string selectedGpuName{selectedName.data()};

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
        std::string dll;
        if (opts.dll)
            dll = *opts.dll;
        else {
            const auto configPath = ls::findConfigurationFile();
            if (std::filesystem::exists(configPath)) {
                const ls::ConfigFile configuration{configPath};
                if (configuration.global().dll)
                    dll = *configuration.global().dll;
            }
        }
        if (dll.empty())
            dll = ls::findShaderDll();
        mako::backend::Instance backend{
            [&selectedGpuName](const std::string& gpuName,
                    std::pair<const std::string&, const std::string&>,
                    const std::optional<std::string>&) {
                return selectedGpuName == gpuName;
            },
            dll, opts.allow_fp16
        };
        mako::backend::Context& context = backend.openContext(
            sourceFds, {destinationFd}, syncFd,
            extent.width, extent.height,
            mako::backend::FrameEncoding::Sdr8,
            1.0F / preset.flowScale, preset.performanceMode
        );

        // Prime temporal history with the first frame. Its generated output is
        // intentionally ignored because the second source is not valid yet.
        uploadImage(vk, previousImage, scene.previous);
        sync.signal(vk, 1);
        backend.scheduleFrames(context);
        if (!sync.wait(vk, 2))
            throw ls::error("timed out while priming quality-regression history");

        uploadImage(vk, currentImage, scene.current);
        sync.signal(vk, 3);
        backend.scheduleFrames(context, std::array{0.5F});
        if (!sync.wait(vk, 4))
            throw ls::error("timed out while generating the regression midpoint");

        const auto generated = downloadImage(vk, destinationImage, sync, 4);
        const auto metrics = mako::quality::evaluateImageQuality(scene, generated);
        constexpr mako::quality::ImageQualityThresholds thresholds{};
        const bool passed = mako::quality::passesImageQualityRegression(
            metrics, thresholds
        );

        std::cout << std::fixed << std::setprecision(5)
            << "AMD image-quality regression: " << (passed ? "PASS" : "FAIL") << '\n'
            << "  preset: Flow Scale " << preset.flowScale << ", "
            << preset.multiplier << "x, performance mode off\n"
            << "  GPU: " << selectedGpuName << '\n'
            << "  robustImageAccess2: "
            << (vk.supportsRobustImageAccess2() ? "enabled" : "unavailable") << '\n'
            << "  mean absolute error: " << metrics.meanAbsoluteError
            << " / " << thresholds.maximumMeanAbsoluteError << '\n'
            << "  motion/disocclusion error: "
            << metrics.focusMeanAbsoluteError << " / "
            << thresholds.maximumFocusMeanAbsoluteError << '\n'
            << "  severe focus-error fraction: "
            << metrics.severeFocusErrorFraction << " / "
            << thresholds.maximumSevereFocusErrorFraction << '\n'
            << "  thin-detail error: " << metrics.detailMeanAbsoluteError
            << " / " << thresholds.maximumDetailMeanAbsoluteError << '\n';

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
