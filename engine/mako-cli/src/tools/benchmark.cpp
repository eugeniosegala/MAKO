/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "benchmark.hpp"
#include "benchmark_input.hpp"
#include "profile_statistics.hpp"
#include "image_transfer.hpp"
#include "mako-common/quality/image_quality.hpp"
#include "i18n.hpp"
#include "mako-backend/mako.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/file_descriptors.hpp"
#include "mako-common/helpers/paths.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/timeline_semaphore.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <map>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include <time.h>
#include <vulkan/vulkan_core.h>

using namespace mako::cli;
using namespace mako::cli::benchmark;

namespace {
    using ProfileClock = std::chrono::steady_clock;
    double elapsedUs(const ProfileClock::time_point start) {
        return std::chrono::duration<double, std::micro>(ProfileClock::now() - start).count();
    }

    void profile(const Options& opts, const vk::Vulkan& vk,
            const vk::TimelineSemaphore& sync, mako::backend::Instance& backend,
            mako::backend::Context& context, const size_t outputs) {
        backend.enableFrameProfiling(context);
        std::map<std::string, std::vector<double>> samples;
        for (const auto* name : {"cpu_signal", "cpu_schedule", "cpu_previous_wait",
                "cpu_prepass_submit", "cpu_generation_submit", "cpu_output_wait",
                "cpu_iteration", "cpu_profile_read", "gpu_prepass", "gpu_generation", "gpu_span",
                "gpu_input_conversion", "gpu_motion_estimation", "gpu_synthesis", "gpu_output_conversion"})
            samples[name].reserve(static_cast<size_t>(opts.profile_samples));
        for (size_t i = 0; i < outputs; ++i)
            samples["gpu_output_" + std::to_string(i)].reserve(static_cast<size_t>(opts.profile_samples));
        uint64_t timeline{1};
        mako::backend::FrameProfile result;
        for (int iteration = 0; iteration < opts.profile_warmup + opts.profile_samples; ++iteration) {
            const auto batchStart = ProfileClock::now();
            auto start = batchStart;
            sync.signal(vk, timeline++);
            const double signalUs = elapsedUs(start);
            start = ProfileClock::now();
            backend.scheduleFrames(context);
            const double scheduleUs = elapsedUs(start);
            start = ProfileClock::now();
            for (size_t i = 0; i < outputs; ++i)
                if (!sync.wait(vk, timeline++))
                    throw ls::error("Frame-profile output wait failed");
            const double waitUs = elapsedUs(start);
            const double iterationUs = elapsedUs(batchStart);
            start = ProfileClock::now();
            result = backend.readFrameProfile(context);
            const double readUs = elapsedUs(start);
            if (iteration < opts.profile_warmup) continue;
            samples.at("cpu_signal").push_back(signalUs);
            samples.at("cpu_schedule").push_back(scheduleUs);
            samples.at("cpu_previous_wait").push_back(result.cpuPreviousWaitUs);
            samples.at("cpu_prepass_submit").push_back(result.cpuPrepassSubmitUs);
            samples.at("cpu_generation_submit").push_back(result.cpuGenerationSubmitUs);
            samples.at("cpu_output_wait").push_back(waitUs);
            samples.at("cpu_iteration").push_back(iterationUs);
            samples.at("cpu_profile_read").push_back(readUs);
            samples.at("gpu_prepass").push_back(result.gpuPrepassUs);
            samples.at("gpu_input_conversion").push_back(result.gpuInputConversionUs);
            samples.at("gpu_motion_estimation").push_back(result.gpuMotionEstimationUs);
            samples.at("gpu_synthesis").push_back(std::accumulate(result.gpuSynthesisUs.begin(), result.gpuSynthesisUs.end(), 0.0));
            samples.at("gpu_output_conversion").push_back(std::accumulate(result.gpuOutputConversionUs.begin(), result.gpuOutputConversionUs.end(), 0.0));
            samples.at("gpu_generation").push_back(std::accumulate(result.gpuGeneratedUs.begin(), result.gpuGeneratedUs.end(), 0.0));
            samples.at("gpu_span").push_back(result.gpuSpanUs);
            for (size_t i = 0; i < outputs; ++i)
                samples.at("gpu_output_" + std::to_string(i)).push_back(result.gpuGeneratedUs.at(i));
        }
        std::cerr << std::fixed << std::setprecision(9)
            << "MAKO Renderer: benchmark-profile operation=summary schema=2 encoding="
            << (opts.profile_hdr10 ? "hdr10-pq-packed" : "sdr-8-bit") << " samples=" << opts.profile_samples
            << " warmup=" << opts.profile_warmup << " outputs=" << outputs
            << " timestamp_bits=" << result.timestampValidBits
            << " timestamp_period_ns=" << result.timestampPeriodNs << '\n';
        for (int i = 0; i < opts.profile_samples; ++i) {
            std::cerr << "MAKO Renderer: benchmark-profile operation=sample index=" << i;
            for (const auto& [name, values] : samples)
                std::cerr << ' ' << name << '=' << values.at(static_cast<size_t>(i));
            std::cerr << '\n';
        }
        for (const auto& [name, values] : samples) {
            const auto stats = mako::cli::profileStatistics(values);
            std::cerr << "MAKO Renderer: benchmark-profile operation=metric name=" << name
                << " unit=us samples=" << values.size()
                << " min=" << stats.minimum << " median=" << stats.median
                << " p95=" << stats.percentile95 << " max=" << stats.maximum
                << " cv_percent=" << stats.coefficientOfVariationPercent << '\n';
        }
        std::cerr << "MAKO Renderer: frame profile is diagnostic; query reads perturb iteration spacing; "
                     "CPU waits overlap GPU work; timings are not additive or comparable capacity FPS\n";
    }
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
        if (opts.profile_samples < 1 || opts.profile_samples > 10000 ||
                opts.profile_warmup < 6 || opts.profile_warmup > 10000)
            throw ls::error("Invalid frame-profile sample or warm-up count");
        if (opts.profile_hdr10 && !opts.profile)
            throw ls::error("HDR10 inputs require diagnostic frame profiling");
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

        std::array<int, 2> srcfds{-1, -1};
        const VkFormat imageFormat = opts.profile_hdr10
            ? VK_FORMAT_A2B10G10R10_UNORM_PACK32 : VK_FORMAT_R8G8B8A8_UNORM;
        ls::FileDescriptorScope sourceScope{srcfds};
        const vk::Image frame_0{vk,
            extent, imageFormat,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &srcfds[0]};
        const vk::Image frame_1{vk,
            extent, imageFormat,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &srcfds[1]};

        std::vector<vk::Image> destimgs{};
        std::vector<int> destfds(opts.multiplier - 1, -1);
        ls::FileDescriptorScope destinationScope{destfds};
        destimgs.reserve(destfds.size());
        for (int& fd : destfds) {
            destimgs.emplace_back(vk,
                extent, imageFormat,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt,
                &fd
            );
        }

        int syncfd{-1};
        ls::FileDescriptorScope syncScope{{&syncfd, 1}};
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
        if (opts.profile_hdr10 && !mako.supportsPackedHdr10Transport())
            throw ls::error("Packed HDR10 profiling is unsupported on the selected backend device");
        mako::backend::Context& mako_ctx = mako.openContext(
            (sourceScope.release(), std::pair{srcfds[0], srcfds[1]}),
            (destinationScope.release(), destfds),
            (syncScope.release(), syncfd), extent.width, extent.height,
            opts.profile_hdr10 ? mako::backend::FrameEncoding::Hdr10PqPacked : mako::backend::FrameEncoding::Sdr8,
            1.0F / opts.flow, opts.performance_mode
        );

        // Defined inputs belong outside the timed capacity loop. This remains a
        // repeated endpoint-pair workload, not a moving game or pixel-quality test.
        {
            const auto scene = mako::quality::makeImageQualityRegressionScene(
                mako::quality::QualitySceneKind::Traffic, extent.width, extent.height,
                0.0F, 1.0F, 0.5F);
            if (opts.profile_hdr10) {
                images::uploadImage(vk, frame_0, hdr10ProfileInput(scene.previous));
                images::uploadImage(vk, frame_1, hdr10ProfileInput(scene.current));
            } else {
                images::uploadImage(vk, frame_0, scene.previous);
                images::uploadImage(vk, frame_1, scene.current);
            }
        }
        std::cerr << "MAKO_BENCHMARK recipe=2 content="
                  << (opts.profile_hdr10 ? "traffic-pair-hdr10-203nit-v1" : "traffic-pair-v1")
                  << " source_times=0,1 upload=outside-timer precision="
                  << (opts.allow_fp16 ? "fp16-allowed" : "fp32") << '\n';

        if (opts.profile) {
            profile(opts, vk, sync, mako, mako_ctx, destimgs.size());
            mako.closeContext(mako_ctx);
            return EXIT_SUCCESS;
        }

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
