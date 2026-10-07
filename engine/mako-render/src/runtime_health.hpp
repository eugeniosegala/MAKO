/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/vulkan/device_memory_accounting.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace mako::layer::present_diagnostics {

struct DrmRenderNode {
    uint32_t major{};
    uint32_t minor{};
};

struct RuntimeHealthPaths {
    std::filesystem::path proc{"/proc"};
    std::filesystem::path sys{"/sys"};
    // The actual Vulkan physical device's render node, never a guessed card0.
    std::optional<DrmRenderNode> gpu;
};

struct RuntimeHealthSample {
    enum class ClockSource { Unknown, Hwmon, Dpm };
    uint64_t sessionMs{};
    uint64_t monotonicMs{};
    double sampleMs{};
    std::optional<double> sampleCpuUs;
    std::optional<double> previousEmitMs;
    uint64_t nextSampleDelayMs{5000};
    std::optional<uint64_t> rssBytes, virtualBytes, swapBytes, threads;
    std::optional<uint64_t> fds;
    bool fdsCapped{};
    std::optional<uint64_t> cpuTicks, minorFaults, majorFaults;
    std::optional<uint64_t> readBytes, writeBytes, cpu0FrequencyKhz, cpu0MaxFrequencyKhz;
    std::optional<double> cpuPercent;
    std::optional<uint64_t> availableBytes, swapFreeBytes, swapTotalBytes;
    std::optional<double> memoryPressureSome, memoryPressureFull, ioPressureFull;
    std::optional<double> cpuPressureSome;
    std::optional<uint64_t> gpuBusyPercent, vramUsedBytes, vramTotalBytes;
    std::optional<uint64_t> gttUsedBytes, gttTotalBytes, gpuFrequencyHz;
    std::optional<uint64_t> gpuMemoryFrequencyHz;
    ClockSource gpuClockSource{ClockSource::Unknown};
    std::optional<int64_t> temperatureMc, criticalTemperatureMc;
    std::optional<uint64_t> powerUw, powerCapUw;
    std::optional<vk::DeviceMemorySnapshot> applicationMemory, backendMemory;
};

// Files are allowlisted and size bounded; missing, denied, malformed, and
// unsupported readings remain unknown. No Vulkan calls or GPU query pools.
RuntimeHealthSample readRuntimeHealth(const RuntimeHealthPaths& paths);
// Reduce polling when collection/output is slow, without changing the game's
// pacing, settings, or recovery. Healthy collection keeps the five-second rate.
std::chrono::milliseconds runtimeHealthDelay(std::chrono::milliseconds normal,
    double sampleMs, std::optional<double> sampleCpuUs, std::optional<double> previousEmitMs);
std::string formatRuntimeHealth(const RuntimeHealthSample& sample,
    uint64_t monitor, std::string_view role, const RuntimeHealthPaths& paths,
    long clockTicksPerSecond, uintptr_t applicationDevice = 0);

// Construct only under the existing presentation-diagnostics flag. One
// outstanding sample, no frame requests or backlog. The worker retains only
// shared atomic accounting, never a Vulkan device or backend owner. Teardown
// joins it before Root's other members are destroyed; an outstanding driver
// file read may delay teardown, but cannot block presentation.
class RuntimeHealthMonitor {
public:
    using Sink = std::function<void(const RuntimeHealthSample&)>;
    RuntimeHealthMonitor(RuntimeHealthPaths paths,
        std::shared_ptr<const vk::DeviceMemoryAccounting> application,
        std::shared_ptr<const vk::DeviceMemoryAccounting> backend,
        Sink sink, std::chrono::milliseconds interval = std::chrono::seconds(5));
    ~RuntimeHealthMonitor();
    RuntimeHealthMonitor(const RuntimeHealthMonitor&) = delete;
    RuntimeHealthMonitor& operator=(const RuntimeHealthMonitor&) = delete;
private:
    std::mutex mutex;
    std::condition_variable_any wake;
    std::jthread worker;
};

}
