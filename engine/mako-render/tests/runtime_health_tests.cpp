/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "runtime_health.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using namespace mako::layer::present_diagnostics;
using namespace std::chrono_literals;

namespace {
void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}
struct Fixture {
    std::filesystem::path root;
    RuntimeHealthPaths paths;
    Fixture() {
        char pattern[] = "/tmp/mako-runtime-health.XXXXXX";
        const auto directory = mkdtemp(pattern);
        expect(directory != nullptr, "temporary fixture creation");
        root = directory;
        paths = {root / "proc", root / "sys", DrmRenderNode{226, 129}};
    }
    ~Fixture() { std::filesystem::remove_all(root); }
    void put(const std::filesystem::path& path, const std::string_view content) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << content;
    }
    std::filesystem::path device() const {
        return paths.sys / "dev/char/226:129/device";
    }
};
}

int main() {
    Fixture fixture;
    fixture.put(fixture.paths.proc / "self/status",
        "Name:\tprivate-game-name\nVmRSS:\t1024 kB\nVmSize:\t4096 kB\n"
        "VmSwap:\t0 kB\nThreads:\t8\n");
    fixture.put(fixture.paths.proc / "self/stat",
        "42 (game name ) with parentheses) S 1 2 3 4 5 6 77 8 9 10 120 30\n");
    fixture.put(fixture.paths.proc / "self/fd/0", "");
    fixture.put(fixture.paths.proc / "self/fd/1", "");
    fixture.put(fixture.paths.proc / "self/io", "read_bytes: 1234\nwrite_bytes: 5678\n");
    fixture.put(fixture.paths.proc / "pressure/cpu", "some avg10=2.50 avg60=1 total=42\n");
    fixture.put(fixture.paths.sys / "devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "1800000\n");
    fixture.put(fixture.paths.sys / "devices/system/cpu/cpu0/cpufreq/scaling_max_freq", "3500000\n");
    fixture.put(fixture.paths.proc / "meminfo",
        "MemAvailable: 8192 kB\nSwapTotal: 1024 kB\nSwapFree: 512 kB\n");
    fixture.put(fixture.paths.proc / "pressure/memory",
        "some avg10=1.25 avg60=0.1 total=42\nfull avg10=0.50 avg60=0.1 total=4\n");
    fixture.put(fixture.paths.proc / "pressure/io", "full avg10=0.75 avg60=0.1 total=4\n");
    const auto device = fixture.device();
    fixture.put(device / "gpu_busy_percent", "0\n");
    fixture.put(device / "mem_info_vram_used", "123456\n");
    fixture.put(device / "mem_info_vram_total", "999999\n");
    fixture.put(device / "mem_info_gtt_used", "654321\n");
    fixture.put(device / "mem_info_gtt_total", "888888\n");
    fixture.put(device / "pp_dpm_sclk", "0: 200Mhz\n1: 1200Mhz *\n");
    const auto sensor = device / "hwmon/hwmon7";
    fixture.put(sensor / "name", "amdgpu\n");
    fixture.put(sensor / "temp1_input", "65000\n");
    fixture.put(sensor / "temp1_crit", "95000\n");
    fixture.put(sensor / "power1_average", "15000000\n");
    fixture.put(sensor / "power1_cap", "20000000\n");
    fixture.put(sensor / "freq2_input", "800000000\n");
    // An unrelated GPU must never supply a fallback reading.
    fixture.put(fixture.paths.sys / "dev/char/226:128/device/gpu_busy_percent", "99\n");
    fixture.put(fixture.paths.sys / "class/drm/card0/device/gpu_busy_percent", "98\n");

    auto sample = readRuntimeHealth(fixture.paths);
    expect(sample.rssBytes == 1048576 && sample.virtualBytes == 4194304 &&
        sample.swapBytes == 0 && sample.threads == 8 && sample.fds == 2,
        "process units and valid zero readings");
    expect(sample.readBytes == 1234 && sample.writeBytes == 5678 &&
        sample.cpuPressureSome == 2.5 && sample.cpu0FrequencyKhz == 1800000 &&
        sample.cpu0MaxFrequencyKhz == 3500000 && sample.gpuMemoryFrequencyHz == 800000000,
        "bounded I/O, CPU pressure, and CPU/memory clock observations");
    expect(sample.cpuTicks == 150 && sample.minorFaults == 77 && sample.majorFaults == 9,
        "stat field positions survive spaces and parentheses in comm");
    expect(sample.availableBytes == 8388608 && sample.swapFreeBytes == 524288 &&
        sample.memoryPressureSome == 1.25 && sample.memoryPressureFull == .5 &&
        sample.ioPressureFull == .75, "system memory and PSI samples");
    expect(sample.gpuBusyPercent == 0 && sample.vramUsedBytes == 123456 &&
        sample.gttUsedBytes == 654321 && sample.gpuFrequencyHz == 1200000000 &&
        sample.temperatureMc == 65000 && sample.criticalTemperatureMc == 95000 &&
        sample.powerUw == 15000000 && sample.powerCapUw == 20000000,
        "actual render node and documented AMD telemetry units");
    fixture.put(sensor / "freq1_input", "1300000000\n");
    expect(readRuntimeHealth(fixture.paths).gpuFrequencyHz == 1300000000,
        "hwmon measured clock precedes the active DPM level");
    auto unknownGpu = fixture.paths;
    unknownGpu.gpu.reset();
    expect(!readRuntimeHealth(unknownGpu).gpuBusyPercent, "no guessed GPU without DRM properties");
    unknownGpu.gpu = DrmRenderNode{226, 130};
    expect(!readRuntimeHealth(unknownGpu).gpuBusyPercent, "missing selected GPU never becomes card0");

    fixture.put(device / "gpu_busy_percent", "101\n");
    fixture.put(device / "mem_info_vram_used", "-1\n");
    fixture.put(device / "mem_info_gtt_used", "123junk\n");
    fixture.put(sensor / "temp1_input", "-5000\n");
    fixture.put(fixture.paths.proc / "self/status",
        "VmRSS: 18446744073709551615 kB\nVmSize: 32 bananas\nVmSwap: 0 kB\n");
    fixture.put(fixture.paths.proc / "pressure/memory",
        "some avg10=NaN\nfull avg10=101\n");
    fixture.put(fixture.paths.proc / "self/stat", "42 (broken) S 1\n");
    sample = readRuntimeHealth(fixture.paths);
    expect(!sample.rssBytes && !sample.virtualBytes && !sample.threads &&
        !sample.cpuTicks && !sample.memoryPressureSome && !sample.memoryPressureFull &&
        !sample.gpuBusyPercent && !sample.vramUsedBytes && !sample.gttUsedBytes &&
        sample.temperatureMc == -5000, "malformed/overflow fields stay unknown, signed temperature valid");
    fixture.put(sensor / "name", std::string(17000, 'x'));
    expect(!readRuntimeHealth(fixture.paths).temperatureMc, "oversized sensor metadata rejected");
    fixture.put(sensor / "name", "amdgpu\n");
    fixture.put(device / "hwmon/hwmon8/name", "amdgpu\n");
    expect(!readRuntimeHealth(fixture.paths).temperatureMc, "ambiguous hwmon mapping rejected");
    std::filesystem::remove_all(device / "hwmon/hwmon8");
    std::filesystem::remove(sensor / "power1_average");
    expect(mkfifo((sensor / "power1_average").c_str(), 0600) == 0, "FIFO fixture");
    expect(!readRuntimeHealth(fixture.paths).powerUw, "special files never block the reader");
    std::filesystem::remove(sensor / "power1_average");
    fixture.put(sensor / "power1_average", "0\n");
    fixture.put(device / "pp_dpm_sclk", "0: 200Mhz *\n1: 1200Mhz *\n");
    std::filesystem::remove(sensor / "freq1_input");
    expect(!readRuntimeHealth(fixture.paths).gpuFrequencyHz, "ambiguous DPM readings rejected");
    for (int fd = 2; fd < 4100; ++fd)
        fixture.put(fixture.paths.proc / "self/fd" / std::to_string(fd), "");
    sample = readRuntimeHealth(fixture.paths);
    expect(sample.fds == 4096 && sample.fdsCapped, "FD enumeration has an explicit bound");
    const auto line = formatRuntimeHealth(sample, 3, "frame-generation", fixture.paths, 100);
    expect(line.starts_with("MAKO Renderer: present diagnostics: operation=runtime-health ") &&
        line.find("gpu_render_node=226:129") != line.npos &&
        line.find("process_rss_bytes=unknown") != line.npos &&
        line.find("device_power_uw=0") != line.npos &&
        line.find("mako_backend_internal_bytes=unknown") != line.npos &&
        line.find("gpu_metrics_scope=whole-device") != line.npos &&
        line.find("private-game-name") == line.npos && line.find(fixture.root.string()) == line.npos,
        "machine-readable output preserves zero/unknown, scopes, and privacy");

    // Real worker, retained canonical counters, first sink failure, continued
    // samples, and prompt cancellation of its long idle wait.
    // The deliberately huge FD fixture above can legitimately trigger cost
    // backoff under sanitizers; use a normal process-size fixture here.
    std::filesystem::remove_all(fixture.paths.proc / "self/fd");
    fixture.put(fixture.paths.proc / "self/fd/0", "");
    auto application = std::make_shared<vk::DeviceMemoryAccounting>();
    auto backend = std::make_shared<vk::DeviceMemoryAccounting>();
    application->recordAllocation(vk::DeviceMemoryKind::Exported, 1000);
    backend->recordAllocation(vk::DeviceMemoryKind::Internal, 2000);
    backend->recordAllocation(vk::DeviceMemoryKind::Imported, 1000);
    std::weak_ptr<const vk::DeviceMemoryAccounting> retained = backend;
    std::promise<RuntimeHealthSample> result;
    auto future = result.get_future();
    std::atomic<int> calls{};
    {
        RuntimeHealthMonitor monitor(fixture.paths, application, backend,
            [&](const RuntimeHealthSample& reading) {
                const auto call = ++calls;
                if (call == 1) throw std::runtime_error("injected sink failure");
                if (call == 2) result.set_value(reading);
            }, 10ms);
        backend.reset();
        expect(future.wait_for(4s) == std::future_status::ready, "sink failure does not kill worker");
        const auto reading = future.get();
        expect(!retained.expired() && reading.applicationMemory->exported.bytes == 1000 &&
            reading.backendMemory->internal.bytes == 2000 &&
            reading.backendMemory->imported.bytes == 1000 && reading.sessionMs > 0,
            "worker samples independently owned counters with monotonic elapsed time");
        const auto accountingLine = formatRuntimeHealth(reading, 1, "frame-generation", fixture.paths, 100);
        expect(accountingLine.find("mako_backend_internal_bytes=2000") != accountingLine.npos &&
            accountingLine.find("mako_backend_imported_bytes=1000") != accountingLine.npos &&
            accountingLine.find("mako_application_exported_bytes=1000") != accountingLine.npos,
            "imported mappings remain separate from owned bytes");
    }
    expect(retained.expired(), "joined worker releases retained counters");
    expect(runtimeHealthDelay(5s, 1, 1000, {}) == 5s &&
        runtimeHealthDelay(5s, 25, 1000, {}) == 30s &&
        runtimeHealthDelay(5s, 1, 5000, {}) == 30s &&
        runtimeHealthDelay(5s, 1, {}, 25) == 30s &&
        runtimeHealthDelay(60s, 25, {}, {}) == 60s,
        "slow I/O, CPU collection, or output backs off only the diagnostic worker");
    std::promise<RuntimeHealthSample> slowOutput;
    auto slowFuture = slowOutput.get_future();
    size_t slowCalls{};
    {
        RuntimeHealthMonitor monitor(fixture.paths, {}, {},
            [&](const auto& reading) {
                if (++slowCalls == 1) std::this_thread::sleep_for(30ms);
                else if (slowCalls == 2) slowOutput.set_value(reading);
            }, 10ms);
        expect(slowFuture.wait_for(4s) == std::future_status::ready, "slow-output observation missing");
        const auto reading = slowFuture.get();
        expect(reading.previousEmitMs && *reading.previousEmitMs >= 25 &&
            reading.nextSampleDelayMs == 30000,
            "real worker measures slow output and backs off instead of queuing retries");
    }
    std::promise<void> initial;
    auto initialFuture = initial.get_future();
    const auto started = std::chrono::steady_clock::now();
    {
        RuntimeHealthMonitor monitor(fixture.paths, {}, {},
            [&](const auto& reading) {
                expect(reading.sampleCpuUs && *reading.sampleCpuUs >= 0 &&
                    reading.monotonicMs > 0 && !reading.previousEmitMs,
                    "worker measures its own CPU and correlation clock without a fake initial emit cost");
                initial.set_value();
            });
        expect(initialFuture.wait_for(4s) == std::future_status::ready, "baseline is immediate");
    }
    expect(std::chrono::steady_clock::now() - started < 4s,
        "idle shutdown does not wait for the five-second period");
    std::cout << "runtime health tests passed\n";
}
