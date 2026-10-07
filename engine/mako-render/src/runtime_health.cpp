/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "runtime_health.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fcntl.h>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace mako::layer::present_diagnostics {
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t readLimit = 16384;
constexpr uint64_t fdLimit = 4096;

std::optional<double> threadCpuUs() {
    timespec time{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) != 0)
        return {};
    return static_cast<double>(time.tv_sec) * 1000000 + time.tv_nsec / 1000.0;
}

std::optional<std::string> readFile(const std::filesystem::path& path) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return {};
    struct Close { int fd; ~Close() { close(fd); } } closeFile{fd};
    struct stat status{};
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode))
        return {};
    std::array<char, readLimit + 1> data{};
    size_t size = 0;
    while (size < data.size()) {
        const auto count = read(fd, data.data() + size, data.size() - size);
        if (count < 0)
            return {};
        if (count == 0)
            return std::string(data.data(), size);
        size += static_cast<size_t>(count);
    }
    return {}; // Oversized input, rather than silently parsing a partial file.
}

std::string_view trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == value.npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

template<typename T> std::optional<T> number(std::string_view value) {
    value = trim(value);
    if (value.empty())
        return {};
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return {};
    return result;
}

template<typename T> std::optional<T> readNumber(const std::filesystem::path& path) {
    const auto content = readFile(path);
    return content ? number<T>(*content) : std::nullopt;
}

std::optional<uint64_t> field(const std::optional<std::string>& text,
        const std::string_view key, const bool kilobytes = false) {
    if (!text)
        return {};
    std::string_view remaining(*text);
    while (!remaining.empty()) {
        const auto newline = remaining.find('\n');
        const auto line = remaining.substr(0, newline);
        if (line.starts_with(key)) {
            auto value = trim(line.substr(key.size()));
            if (kilobytes) {
                if (!value.ends_with(" kB"))
                    return {};
                value = trim(value.substr(0, value.size() - 3));
            }
            const auto parsed = number<uint64_t>(value);
            const uint64_t scale = kilobytes ? 1024 : 1;
            return parsed && *parsed <= std::numeric_limits<uint64_t>::max() / scale
                ? std::optional<uint64_t>(*parsed * scale) : std::nullopt;
        }
        if (newline == remaining.npos)
            break;
        remaining.remove_prefix(newline + 1);
    }
    return {};
}

std::optional<double> pressure(const std::optional<std::string>& text,
        const std::string_view category) {
    if (!text)
        return {};
    std::istringstream input(*text);
    input.imbue(std::locale::classic());
    std::string line;
    while (std::getline(input, line)) {
        if (!std::string_view(line).starts_with(category))
            continue;
        std::istringstream fields(line);
        std::string token;
        while (fields >> token) {
            if (!token.starts_with("avg10="))
                continue;
            std::istringstream value(token.substr(6));
            value.imbue(std::locale::classic());
            double result{};
            if (value >> result && value.eof() && std::isfinite(result) &&
                    result >= 0 && result <= 100)
                return result;
        }
    }
    return {};
}

std::optional<uint64_t> activeClock(const std::filesystem::path& path) {
    const auto content = readFile(path);
    if (!content)
        return {};
    std::istringstream lines(*content);
    std::optional<uint64_t> selected;
    std::string line;
    while (std::getline(lines, line)) {
        if (!trim(line).ends_with('*'))
            continue;
        const auto colon = line.find(':');
        if (colon == line.npos)
            return {};
        auto value = trim(std::string_view(line).substr(colon + 1));
        value = trim(value.substr(0, value.size() - 1));
        if (!value.ends_with("Mhz") && !value.ends_with("MHz"))
            return {};
        const auto mhz = number<uint64_t>(value.substr(0, value.size() - 3));
        if (selected || !mhz || *mhz > std::numeric_limits<uint64_t>::max() / 1000000)
            return {};
        selected = *mhz * 1000000;
    }
    return selected;
}

std::optional<std::filesystem::path> hwmon(const std::filesystem::path& device) {
    std::error_code error;
    std::optional<std::filesystem::path> selected;
    size_t count{};
    for (std::filesystem::directory_iterator entry(device / "hwmon", error), end;
            !error && entry != end; entry.increment(error)) {
        if (++count > 32)
            return {};
        const auto name = readFile(entry->path() / "name");
        if (name && trim(*name) == "amdgpu") {
            if (selected)
                return {}; // Ambiguous sensors must not be attributed to the GPU.
            selected = entry->path();
        }
    }
    return error ? std::nullopt : selected;
}

template<typename T> void metric(std::ostream& line, const std::string_view name,
        const std::optional<T>& value) {
    line << ' ' << name << '=';
    if (value)
        line << *value;
    else
        line << "unknown";
}

void memory(std::ostream& line, const std::string_view owner,
        const std::optional<vk::DeviceMemorySnapshot>& snapshot) {
    const auto category = [&](const std::string_view name,
            const vk::DeviceMemoryTotals vk::DeviceMemorySnapshot::*current,
            const vk::DeviceMemoryTotals vk::DeviceMemorySnapshot::*peak) {
        const auto prefix = std::string(owner) + '_' + std::string(name);
        metric(line, prefix + "_bytes", snapshot ? std::optional(((*snapshot).*current).bytes) : std::nullopt);
        metric(line, prefix + "_allocations", snapshot ? std::optional(((*snapshot).*current).allocations) : std::nullopt);
        metric(line, prefix + "_peak_bytes", snapshot ? std::optional(((*snapshot).*peak).bytes) : std::nullopt);
    };
    category("internal", &vk::DeviceMemorySnapshot::internal, &vk::DeviceMemorySnapshot::peakInternal);
    category("imported", &vk::DeviceMemorySnapshot::imported, &vk::DeviceMemorySnapshot::peakImported);
    category("exported", &vk::DeviceMemorySnapshot::exported, &vk::DeviceMemorySnapshot::peakExported);
}
}

RuntimeHealthSample readRuntimeHealth(const RuntimeHealthPaths& paths) {
    RuntimeHealthSample sample;
    const auto status = readFile(paths.proc / "self/status");
    sample.rssBytes = field(status, "VmRSS:", true);
    sample.virtualBytes = field(status, "VmSize:", true);
    sample.swapBytes = field(status, "VmSwap:", true);
    sample.threads = field(status, "Threads:");
    const auto io = readFile(paths.proc / "self/io");
    sample.readBytes = field(io, "read_bytes:");
    sample.writeBytes = field(io, "write_bytes:");
    const auto stat = readFile(paths.proc / "self/stat");
    if (stat) {
        // comm can contain spaces and ')' characters. Fields start after its
        // final ')'; never include the game name or command line in the log.
        const auto close = stat->rfind(')');
        if (close != stat->npos) {
            std::istringstream values(stat->substr(close + 1));
            std::string value;
            std::optional<uint64_t> user, system;
            for (int index = 3; index <= 15 && values >> value; ++index) {
                if (index == 10) sample.minorFaults = number<uint64_t>(value);
                if (index == 12) sample.majorFaults = number<uint64_t>(value);
                if (index == 14) user = number<uint64_t>(value);
                if (index == 15) system = number<uint64_t>(value);
            }
            if (user && system && *user <= std::numeric_limits<uint64_t>::max() - *system)
                sample.cpuTicks = *user + *system;
        }
    }
    std::error_code error;
    uint64_t fds{};
    for (std::filesystem::directory_iterator entry(paths.proc / "self/fd", error), end;
            !error && entry != end; entry.increment(error)) {
        if (fds == fdLimit) {
            sample.fdsCapped = true;
            break;
        }
        ++fds;
    }
    if (!error) sample.fds = fds;

    const auto meminfo = readFile(paths.proc / "meminfo");
    sample.availableBytes = field(meminfo, "MemAvailable:", true);
    sample.swapFreeBytes = field(meminfo, "SwapFree:", true);
    sample.swapTotalBytes = field(meminfo, "SwapTotal:", true);
    const auto memoryPressure = readFile(paths.proc / "pressure/memory");
    sample.memoryPressureSome = pressure(memoryPressure, "some ");
    sample.memoryPressureFull = pressure(memoryPressure, "full ");
    sample.ioPressureFull = pressure(readFile(paths.proc / "pressure/io"), "full ");
    sample.cpuPressureSome = pressure(readFile(paths.proc / "pressure/cpu"), "some ");
    const auto cpu0 = paths.sys / "devices/system/cpu/cpu0/cpufreq";
    sample.cpu0FrequencyKhz = readNumber<uint64_t>(cpu0 / "scaling_cur_freq");
    sample.cpu0MaxFrequencyKhz = readNumber<uint64_t>(cpu0 / "scaling_max_freq");

    if (paths.gpu) {
        const auto device = paths.sys / "dev/char" /
            (std::to_string(paths.gpu->major) + ':' + std::to_string(paths.gpu->minor)) / "device";
        sample.gpuBusyPercent = readNumber<uint64_t>(device / "gpu_busy_percent");
        if (sample.gpuBusyPercent && *sample.gpuBusyPercent > 100)
            sample.gpuBusyPercent.reset();
        sample.vramUsedBytes = readNumber<uint64_t>(device / "mem_info_vram_used");
        sample.vramTotalBytes = readNumber<uint64_t>(device / "mem_info_vram_total");
        sample.gttUsedBytes = readNumber<uint64_t>(device / "mem_info_gtt_used");
        sample.gttTotalBytes = readNumber<uint64_t>(device / "mem_info_gtt_total");
        if (const auto sensor = hwmon(device)) {
            sample.temperatureMc = readNumber<int64_t>(*sensor / "temp1_input");
            sample.criticalTemperatureMc = readNumber<int64_t>(*sensor / "temp1_crit");
            sample.gpuFrequencyHz = readNumber<uint64_t>(*sensor / "freq1_input");
            sample.gpuMemoryFrequencyHz = readNumber<uint64_t>(*sensor / "freq2_input");
            if (sample.gpuFrequencyHz)
                sample.gpuClockSource = RuntimeHealthSample::ClockSource::Hwmon;
            sample.powerUw = readNumber<uint64_t>(*sensor / "power1_average");
            sample.powerCapUw = readNumber<uint64_t>(*sensor / "power1_cap");
        }
        if (!sample.gpuFrequencyHz) {
            sample.gpuFrequencyHz = activeClock(device / "pp_dpm_sclk");
            if (sample.gpuFrequencyHz)
                sample.gpuClockSource = RuntimeHealthSample::ClockSource::Dpm;
        }
    }
    return sample;
}

std::chrono::milliseconds runtimeHealthDelay(const std::chrono::milliseconds normal,
        const double sampleMs, const std::optional<double> sampleCpuUs,
        const std::optional<double> previousEmitMs) {
    const auto base = std::max(normal, std::chrono::milliseconds(1));
    // A slow driver/sysfs or log sink must not be hammered every five seconds.
    // Keep this local to the diagnostic worker, with no game recovery action.
    if (sampleMs >= 25 || sampleCpuUs.value_or(0) >= 5000 || previousEmitMs.value_or(0) >= 25)
        return std::max(base, std::chrono::milliseconds(30000));
    return base;
}

std::string formatRuntimeHealth(const RuntimeHealthSample& sample,
        const uint64_t monitor, const std::string_view role,
        const RuntimeHealthPaths& paths, const long clockTicksPerSecond,
        const uintptr_t applicationDevice) {
    std::ostringstream line;
    line.imbue(std::locale::classic());
    line << "MAKO Renderer: present diagnostics: operation=runtime-health"
         << " monitor=" << monitor << " pid=" << getpid() << " role=" << role
         << " device=" << reinterpret_cast<const void*>(applicationDevice)
         << " session_ms=" << sample.sessionMs << " sample_ms=" << sample.sampleMs
         << " monotonic_ms=" << sample.monotonicMs
         << " next_sample_delay_ms=" << sample.nextSampleDelayMs
         << " gpu_scope=application-device gpu_render_node=";
    if (paths.gpu) line << paths.gpu->major << ':' << paths.gpu->minor;
    else line << "unknown";
    line << " gpu_metrics_scope=whole-device power_scope=device-or-apu";
    metric(line, "process_cpu_ticks_per_second", clockTicksPerSecond > 0
        ? std::optional<long>(clockTicksPerSecond) : std::nullopt);
    metric(line, "sample_cpu_us", sample.sampleCpuUs);
    metric(line, "previous_emit_ms", sample.previousEmitMs);
    metric(line, "process_rss_bytes", sample.rssBytes);
    metric(line, "process_virtual_bytes", sample.virtualBytes);
    metric(line, "process_swap_bytes", sample.swapBytes);
    metric(line, "process_threads", sample.threads);
    metric(line, "process_fds", sample.fds);
    line << " process_fds_capped=" << sample.fdsCapped;
    metric(line, "process_cpu_ticks_total", sample.cpuTicks);
    metric(line, "process_cpu_percent", sample.cpuPercent);
    metric(line, "process_minor_faults_total", sample.minorFaults);
    metric(line, "process_major_faults_total", sample.majorFaults);
    metric(line, "process_read_bytes_total", sample.readBytes);
    metric(line, "process_write_bytes_total", sample.writeBytes);
    metric(line, "cpu0_scaling_frequency_khz", sample.cpu0FrequencyKhz);
    metric(line, "cpu0_scaling_max_frequency_khz", sample.cpu0MaxFrequencyKhz);
    metric(line, "system_available_bytes", sample.availableBytes);
    metric(line, "system_swap_free_bytes", sample.swapFreeBytes);
    metric(line, "system_swap_total_bytes", sample.swapTotalBytes);
    metric(line, "system_memory_pressure_some_avg10", sample.memoryPressureSome);
    metric(line, "system_memory_pressure_full_avg10", sample.memoryPressureFull);
    metric(line, "system_io_pressure_full_avg10", sample.ioPressureFull);
    metric(line, "system_cpu_pressure_some_avg10", sample.cpuPressureSome);
    metric(line, "gpu_busy_percent", sample.gpuBusyPercent);
    metric(line, "gpu_vram_used_bytes", sample.vramUsedBytes);
    metric(line, "gpu_vram_total_bytes", sample.vramTotalBytes);
    metric(line, "gpu_gtt_used_bytes", sample.gttUsedBytes);
    metric(line, "gpu_gtt_total_bytes", sample.gttTotalBytes);
    metric(line, "gpu_frequency_hz", sample.gpuFrequencyHz);
    metric(line, "gpu_memory_frequency_hz", sample.gpuMemoryFrequencyHz);
    line << " gpu_clock_source=" << (sample.gpuClockSource == RuntimeHealthSample::ClockSource::Hwmon
        ? "hwmon" : sample.gpuClockSource == RuntimeHealthSample::ClockSource::Dpm ? "active-dpm-level" : "unknown");
    metric(line, "gpu_temperature_mc", sample.temperatureMc);
    metric(line, "gpu_temperature_critical_mc", sample.criticalTemperatureMc);
    metric(line, "device_power_uw", sample.powerUw);
    metric(line, "device_power_cap_uw", sample.powerCapUw);
    memory(line, "mako_application", sample.applicationMemory);
    memory(line, "mako_backend", sample.backendMemory);
    line << '\n';
    return line.str();
}

RuntimeHealthMonitor::RuntimeHealthMonitor(RuntimeHealthPaths paths,
        std::shared_ptr<const vk::DeviceMemoryAccounting> application,
        std::shared_ptr<const vk::DeviceMemoryAccounting> backend, Sink sink,
        const std::chrono::milliseconds interval)
    : worker([this, paths = std::move(paths), application = std::move(application),
            backend = std::move(backend), sink = std::move(sink),
            interval = std::max(interval, std::chrono::milliseconds(1)),
            started = Clock::now()](const std::stop_token stop) {
        const long ticksPerSecond = sysconf(_SC_CLK_TCK);
        std::optional<uint64_t> previousTicks;
        std::optional<Clock::time_point> previousTime;
        std::optional<double> previousEmitMs;
        while (!stop.stop_requested()) {
            const auto now = Clock::now();
            const auto cpuStarted = threadCpuUs();
            auto delay = interval;
            try {
                auto sample = readRuntimeHealth(paths);
                sample.sessionMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
                sample.monotonicMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                if (application) sample.applicationMemory = application->snapshot();
                if (backend) sample.backendMemory = backend->snapshot();
                if (ticksPerSecond > 0 && previousTicks && sample.cpuTicks && previousTime &&
                        *sample.cpuTicks >= *previousTicks && now > *previousTime) {
                    sample.cpuPercent = 100.0 * static_cast<double>(*sample.cpuTicks - *previousTicks) /
                        ticksPerSecond / std::chrono::duration<double>(now - *previousTime).count();
                }
                previousTicks = sample.cpuTicks;
                previousTime = now;
                sample.sampleMs = std::chrono::duration<double, std::milli>(Clock::now() - now).count();
                const auto cpuFinished = threadCpuUs();
                if (cpuStarted && cpuFinished && *cpuFinished >= *cpuStarted)
                    sample.sampleCpuUs = *cpuFinished - *cpuStarted;
                sample.previousEmitMs = previousEmitMs;
                delay = runtimeHealthDelay(interval, sample.sampleMs, sample.sampleCpuUs, previousEmitMs);
                sample.nextSampleDelayMs = delay.count();
                const auto emitStarted = Clock::now();
                // Include record formatting and writing in the next observation.
                // A sink failure still contributes to backoff and never escapes.
                try { sink(sample); } catch (...) {}
                previousEmitMs = std::chrono::duration<double, std::milli>(Clock::now() - emitStarted).count();
            } catch (...) {
                // Allocation, proc/sysfs, and sink failures never affect the game.
                delay = std::max(interval, std::chrono::milliseconds(30000));
            }
            std::unique_lock lock(this->mutex);
            this->wake.wait_for(lock, stop, delay, [] { return false; });
        }
    }) {}

RuntimeHealthMonitor::~RuntimeHealthMonitor() {
    this->worker.request_stop();
    this->worker.join();
}
}
