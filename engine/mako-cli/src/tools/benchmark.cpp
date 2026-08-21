/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "benchmark.hpp"
#include "i18n.hpp"
#include "mako-backend/mako.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/paths.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/timeline_semaphore.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <time.h>
#include <vulkan/vulkan_core.h>

using namespace mako::cli;
using namespace mako::cli::benchmark;

namespace {
    // get current time in milliseconds
    uint64_t ms() {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);

        return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
            static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
    }
}

int benchmark::run(const Options& opts, const i18n::Language language) {
    const i18n::Strings& text = i18n::strings(language);
    try {
        // parse options
        if (opts.flow < 0.25F || opts.flow > 1.0F)
            throw ls::error(std::string{text.flow_scale_range});
        if (opts.multiplier < 2)
            throw ls::error(std::string{text.multiplier_minimum});
        if (opts.width <= 0 || opts.height <= 0)
            throw ls::error(std::string{text.dimensions_positive});
        if (opts.duration <= 0)
            throw ls::error(std::string{text.duration_positive});
        const VkExtent2D extent{
            static_cast<uint32_t>(opts.width),
            static_cast<uint32_t>(opts.height)
        };

        // create instance
        const vk::Vulkan vk{
            "mako-debug", vk::version{2, 0, 0},
            "mako-debug-engine", vk::version{2, 0, 0},
            [opts, &text](const vk::VulkanInstanceFuncs fi,
                    const std::vector<VkPhysicalDevice>& devices) {
                if (!opts.gpu.has_value())
                    return devices.front();

                for (const VkPhysicalDevice& device : devices) {
                    VkPhysicalDeviceProperties2 props{
                        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
                    };
                    fi.GetPhysicalDeviceProperties2(device, &props);

                    auto& properties = props.properties;
                    std::array<char, 256> devname = std::to_array(properties.deviceName);
                    devname.at(255) = '\0'; // ensure null-termination

                    if (std::string(devname.data()) == *opts.gpu)
                        return device;
                }

                throw ls::error(std::string{text.gpu_not_found} + *opts.gpu);
            }
        };

        std::pair<int, int> srcfds{};
        const vk::Image frame_0{vk,
            extent, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &srcfds.first};
        const vk::Image frame_1{vk,
            extent, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &srcfds.second};

        std::vector<vk::Image> destimgs{};
        std::vector<int> destfds{};
        for (int i = 0; i < (opts.multiplier - 1); i++) {
            int fd{};
            destimgs.emplace_back(vk,
                extent, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt,
                &fd
            );
            destfds.push_back(fd);
        }

        int syncfd{};
        const vk::TimelineSemaphore sync{vk, 0, std::nullopt, &syncfd};

        // initialize backend
        std::string dll{};
        if (opts.dll.has_value())
            dll = *opts.dll;
        else
            dll = ls::findShaderDll();

        mako::backend::Instance mako{
            [opts](
                const std::string& gpu_name,
                std::pair<const std::string&, const std::string&>,
                const std::optional<std::string>&
            ) {
                return opts.gpu.value_or(gpu_name) == gpu_name;
            },
            dll, opts.allow_fp16
        };
        mako::backend::Context& mako_ctx = mako.openContext(
            srcfds, destfds,
            syncfd, extent.width, extent.height,
            mako::backend::FrameEncoding::Sdr8,
            1.0F / opts.flow, opts.performance_mode
        );

        // run the benchmark
        size_t iterations{0};
        size_t generated_frames{0};
        size_t total_frames{1};

        uint64_t print_time = ms() + 1000ULL;
        const uint64_t end_time = ms() + static_cast<uint64_t>(opts.duration) * 1000ULL;
        while (ms() < end_time) {
            sync.signal(vk, total_frames++);
            mako.scheduleFrames(mako_ctx);

            for (size_t i = 0; i < destimgs.size(); i++) {
                auto success = sync.wait(vk, total_frames++);
                if (!success)
                    throw ls::error(std::string{text.frame_wait_failed});

                generated_frames++;
            }

            iterations++;

            if (ms() >= print_time) {
                print_time += 1000ULL;
                std::cerr << "." << std::flush;
            }
        }

        // output results

        std::cerr << (opts.duration < 40 ? "\r" : "\n");
        std::cerr << text.benchmark_results << opts.duration
            << text.benchmark_seconds;
        std::cerr << text.benchmark_iterations << iterations << "\n";
        std::cerr << text.benchmark_generated_frames << generated_frames << "\n";
        std::cerr << text.benchmark_total_frames << total_frames << "\n";
        const auto time = static_cast<double>(opts.duration);
        const double fps_generated = static_cast<double>(generated_frames) / time;
        const double fps_total = static_cast<double>(total_frames) / time;
        std::cerr << std::setprecision(2) << std::fixed;
        std::cerr << text.benchmark_generated_fps << fps_generated << "fps\n";
        std::cerr << text.benchmark_total_fps << fps_total << "fps\n";

        // deinitialize mako
        mako.closeContext(mako_ctx);
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << text.error << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
