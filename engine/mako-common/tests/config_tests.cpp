/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/detection.hpp"
#include "mako-common/configuration/launch.hpp"
#include "configuration/launcher_exclusions_generated.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>
#include <sys/resource.h>

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    void writeText(const std::filesystem::path& path, const std::string_view text) {
        std::ofstream output(path, std::ios::trunc);
        output << text;
        output.close();
    }

    std::string readText(const std::filesystem::path& path) {
        std::ifstream input(path);
        return {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
    }

    bool sameGameConf(const ls::GameConf& left, const ls::GameConf& right) {
        return left.name == right.name &&
            left.active_in == right.active_in &&
            left.gpu == right.gpu &&
            left.multiplier == right.multiplier &&
            left.frame_generation_provisioned ==
                right.frame_generation_provisioned &&
            left.frame_generation_enabled == right.frame_generation_enabled &&
            left.scaling_enabled == right.scaling_enabled &&
            left.swapchain_image_count_compatibility ==
                right.swapchain_image_count_compatibility &&
            left.scaling_method == right.scaling_method &&
            left.scaling_factor == right.scaling_factor &&
            left.scaling_supersampling == right.scaling_supersampling &&
            left.scaling_sharpness == right.scaling_sharpness &&
            left.hdr_reduced_precision == right.hdr_reduced_precision &&
            left.frame_generation_refresh_threshold ==
                right.frame_generation_refresh_threshold &&
            left.base_fps_cap == right.base_fps_cap &&
            left.adaptive == right.adaptive &&
            left.adaptive_auto_base_fps_cap ==
                right.adaptive_auto_base_fps_cap &&
            left.adaptive_fractional_real_frame_priority ==
                right.adaptive_fractional_real_frame_priority &&
            left.target_fps == right.target_fps &&
            left.adaptive_target_refresh_rate ==
                right.adaptive_target_refresh_rate &&
            left.adaptive_max_multiplier == right.adaptive_max_multiplier &&
            left.adaptive_stable_cadence == right.adaptive_stable_cadence &&
            left.gamescope_vrr_mode == right.gamescope_vrr_mode &&
            left.dynamic_cadence_recovery == right.dynamic_cadence_recovery &&
            left.dynamic_cadence_probe_interval_seconds ==
                right.dynamic_cadence_probe_interval_seconds &&
            left.ultra_performance == right.ultra_performance &&
            left.flow_scale == right.flow_scale &&
            left.performance_mode == right.performance_mode &&
            left.pacing == right.pacing &&
            left.power_profiles.size() == right.power_profiles.size() &&
            std::equal(left.power_profiles.begin(), left.power_profiles.end(),
                right.power_profiles.begin(), sameGameConf);
    }

    void testOptionalLosslessScaling(const std::filesystem::path& directory) {
        const auto dll = directory / "removed-Lossless.dll";
        const auto path = directory / "optional-lossless.toml";
        ls::ConfigFile fixture;
        fixture.global().dll = dll.string();
        ls::GameConf game;
        game.name = "open-scaling";
        game.active_in = {"Game.exe"};
        game.frame_generation_provisioned = false;
        game.scaling_enabled = true;
        game.scaling_method = ls::ScalingMethod::Mako;
        fixture.profiles() = {game};
        fixture.current_profile = game.name;
        fixture.write(path);
        for (const bool installed : {false, true, false}) {
            if (installed) writeText(dll, "synthetic input");
            else std::filesystem::remove(dll);
            ls::ConfigFile parsed(path);
            expect(parsed.current_profile == game.name,
                "The editor selection must survive native configuration round trips");
            expect(parsed.global().dll == dll.string() && sameGameConf(parsed.profiles().front(), game),
                "DLL removal must not invalidate or reset open-scaling profiles");
            parsed.write(path);
        }
        ls::Identification identity;
        identity.override = game.name;
        expect(ls::findProfile(ls::ConfigFile(path), identity).has_value(),
            "A missing DLL must not prevent native profile matching");
        setenv("MAKO_ENV", "1", 1);
        setenv("MAKO_DLL_PATH", dll.c_str(), 1);
        setenv("MAKO_FRAME_GENERATION_PROVISIONED", "0", 1);
        setenv("MAKO_SCALING_ENABLED", "1", 1);
        setenv("MAKO_SCALING_METHOD", "mako", 1);
        const ls::WatchedConfig environment;
        expect(environment.get().global().dll == dll.string() &&
                environment.get().profiles().front().scaling_method == ls::ScalingMethod::Mako,
            "A missing environment DLL must not block open scaling");
        unsetenv("MAKO_ENV");
        unsetenv("MAKO_DLL_PATH");
        unsetenv("MAKO_FRAME_GENERATION_PROVISIONED");
        unsetenv("MAKO_SCALING_ENABLED");
        unsetenv("MAKO_SCALING_METHOD");
    }

    void testPowerProfiles(const std::filesystem::path& directory) {
        const auto root = directory / "power_supply";
        std::filesystem::create_directories(root / "AC");
        writeText(root / "AC/type", "USB_C\n");
        writeText(root / "AC/online", "0\n");
        expect(ls::detectPowerSource(root) == ls::PowerSource::Unknown,
            "An offline supply without a system battery must be unknown");
        std::filesystem::create_directories(root / "BAT");
        writeText(root / "BAT/type", "Battery\n");
        writeText(root / "BAT/status", "Full\n");
        expect(ls::detectPowerSource(root) == ls::PowerSource::Handheld,
            "A full battery must not imply AC power");
        writeText(root / "BAT/scope", "Device\n");
        expect(ls::detectPowerSource(root) == ls::PowerSource::Unknown,
            "Peripheral batteries must not select Handheld");
        writeText(root / "BAT/scope", "System\n");
        const auto path = directory / "power.toml";
        writeText(path, R"(version = 2
[global]
allow_fp16 = false
[[profile]]
name = "game"
active_in = "Game.exe"
gpu = "Other GPU"
adaptive = true
target_fps = 90
[profile.handheld]
target_fps = 60
adaptive_target_refresh_rate = true
gpu = ""
[profile.docked]
target_fps = 144
)");
        ls::ConfigFile parsed(path);
        expect(parsed.profiles().front().power_profiles.size() == 2,
            "Both native power tables must be parsed");
        parsed.write(path);
        const ls::ConfigFile roundTrip(path);
        expect(roundTrip.profiles().front().power_profiles[0].adaptive &&
                roundTrip.profiles().front().power_profiles[0]
                    .adaptive_target_refresh_rate &&
                !roundTrip.profiles().front().adaptive_target_refresh_rate &&
                !roundTrip.profiles().front().power_profiles[1]
                    .adaptive_target_refresh_rate &&
                !roundTrip.profiles().front().power_profiles[0].gpu &&
                roundTrip.profiles().front().power_profiles[1].target_fps == 144,
            "Sparse power settings must inherit and round-trip, including cleared GPUs");
        ls::Identification identity;
        identity.override = "game";
        for (const auto source : {ls::PowerSource::Unknown, ls::PowerSource::Handheld, ls::PowerSource::Docked}) {
            parsed.power_source = source;
            const auto selected = ls::findProfile(parsed, identity);
            expect(selected && selected->second.target_fps ==
                    (source == ls::PowerSource::Unknown ? 90U : source == ls::PowerSource::Handheld ? 60U : 144U) &&
                    selected->second.name == "game" && selected->second.active_in.front() == "Game.exe" &&
                    selected->second.power_profiles.empty(),
                "Explicit profile selection must resolve power without changing identity");
        }
        setenv("MAKO_CONFIG", path.c_str(), 1);
        ls::WatchedConfig watched(root);
        expect(watched.get().power_source == ls::PowerSource::Handheld,
            "The startup watcher must select battery settings");
        const auto timestamp = std::filesystem::last_write_time(path);
        writeText(root / "AC/online", "1\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(2050));
        bool changed = watched.update();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (watched.get().power_source != ls::PowerSource::Docked &&
                std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            changed = watched.update() || changed;
        }
        expect(changed && watched.get().power_source == ls::PowerSource::Docked &&
                std::filesystem::last_write_time(path) == timestamp,
            "A power transition must reload selection without rewriting TOML");
        writeText(root / "AC/online", "unknown\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(2050));
        expect(!watched.update() && watched.get().power_source == ls::PowerSource::Docked,
            "Transient power read failures must retain the confirmed runtime source");
        for (const auto invalid : {"[profile.handheld]\ntarget_fps = 60\n",
                "[profile.handheld]\nname = 'other'\n[profile.docked]\n",
                "[profile.handheld]\ntarget_fps = 0\n[profile.docked]\n"}) {
            writeText(path, std::string("version = 2\n[[profile]]\nname = 'game'\n") + invalid);
            bool rejected = false;
            try { static_cast<void>(ls::ConfigFile(path)); }
            catch (const std::exception&) { rejected = true; }
            expect(rejected, "Invalid power profiles must be rejected");
        }
    }

    constexpr std::string_view validConfiguration = R"(version = 2
[global]
allow_fp16 = true
removed_global_option = "inert"

[[profile]]
name = "test"
active_in = "game"
removed_profile_option = true
adaptive = false
frame_generation_provisioned = false
scaling_enabled = true
swapchain_image_count_compatibility = true
scaling_method = "ls1"
scaling_factor = 1.75
scaling_supersampling = true
scaling_sharpness = 0.6
frame_generation_refresh_threshold = 60
base_fps_cap = 60
adaptive_auto_base_fps_cap = true
adaptive_fractional_real_frame_priority = "high"
target_fps = 144
adaptive_target_refresh_rate = true
adaptive_max_multiplier = 4
dynamic_cadence_recovery = true
dynamic_cadence_probe_interval_seconds = 0.1
ultra_performance = true
flow_scale = 0.95
performance_mode = false
)";
}

int main() {
    const ls::GameConf defaults;
    expect(defaults.hdr_reduced_precision,
        "Reduced HDR precision must default on for profiles without an explicit choice");
    expect(defaults.multiplier == ls::GameConfDefaults::multiplier &&
            defaults.frame_generation_provisioned ==
                ls::GameConfDefaults::frameGenerationProvisioned &&
            defaults.frame_generation_enabled ==
                ls::GameConfDefaults::frameGenerationEnabled &&
            defaults.scaling_enabled ==
                ls::GameConfDefaults::scalingEnabled &&
            defaults.swapchain_image_count_compatibility ==
                ls::GameConfDefaults::swapchainImageCountCompatibility &&
            defaults.scaling_method ==
                ls::GameConfDefaults::scalingMethod &&
            defaults.scaling_factor == ls::GameConfDefaults::scalingFactor &&
            defaults.scaling_supersampling ==
                ls::GameConfDefaults::scalingSupersampling &&
            defaults.scaling_sharpness ==
                ls::GameConfDefaults::scalingSharpness &&
            defaults.frame_generation_refresh_threshold ==
                ls::GameConfDefaults::frameGenerationRefreshThreshold &&
            defaults.base_fps_cap == ls::GameConfDefaults::baseFpsCap &&
            defaults.adaptive == ls::GameConfDefaults::adaptive &&
            defaults.adaptive_auto_base_fps_cap ==
                ls::GameConfDefaults::adaptiveAutoBaseFpsCap &&
            defaults.adaptive_fractional_real_frame_priority ==
                ls::GameConfDefaults::adaptiveFractionalRealFramePriority &&
            defaults.target_fps == ls::GameConfDefaults::targetFps &&
            !defaults.adaptive_target_refresh_rate &&
            defaults.adaptive_max_multiplier ==
                ls::GameConfDefaults::adaptiveMaxMultiplier &&
            defaults.adaptive_stable_cadence ==
                ls::GameConfDefaults::adaptiveStableCadence &&
            defaults.gamescope_vrr_mode ==
                ls::GameConfDefaults::gamescopeVrrMode &&
            defaults.dynamic_cadence_recovery ==
                ls::GameConfDefaults::dynamicCadenceRecovery &&
            defaults.dynamic_cadence_probe_interval_seconds ==
                ls::GameConfDefaults::dynamicCadenceProbeIntervalSeconds &&
            defaults.ultra_performance ==
                ls::GameConfDefaults::ultraPerformance &&
            defaults.flow_scale == ls::GameConfDefaults::flowScale &&
            defaults.performance_mode ==
                ls::GameConfDefaults::performanceMode &&
            defaults.pacing == ls::GameConfDefaults::pacing,
        "GameConf must use the Renderer profile defaults");
    expect(defaults.scaling_method == ls::ScalingMethod::Ls1,
        "LS1 Quality must be the default spatial scaling method");
    expect(ls::GameConfDefaults::flowScale == 0.8F,
        "standalone Renderer and MAKO Decky must share the 80% Flow Scale default");
    expect(ls::GameConfDefaults::adaptiveStableCadence,
        "Smooth Cadence must default to on for new standalone Renderer profiles");
    expect(ls::scalingMethodFromName("native") ==
                ls::ScalingMethod::Native &&
            std::string_view(ls::scalingMethodName(
                ls::ScalingMethod::Native)) == "native" &&
            !ls::licensedScalingModelRequested(ls::ScalingMethod::Native) &&
            ls::licensedScalingModelRequested(ls::ScalingMethod::Ls1) &&
            ls::licensedScalingModelRequested(
                ls::ScalingMethod::Ls1Performance),
        "Native and licensed scaling method identities must remain explicit");
    auto nativeEngine = defaults;
    nativeEngine.scaling_enabled = true;
    nativeEngine.scaling_method = ls::ScalingMethod::Native;
    expect(ls::spatialScalingRequested(nativeEngine),
        "Native must retain the model-free reconstruction lane for live switching");
    nativeEngine.scaling_factor = 1.0F;
    expect(!ls::spatialScalingMayActivate(nativeEngine),
        "An enabled 1.0 scaler must remain spatially inactive");
    nativeEngine.scaling_factor = 1.5F;
    expect(ls::spatialScalingMayActivate(nativeEngine),
        "An enabled scaler above 1.0 must retain activation provisioning");
    nativeEngine.scaling_method = ls::ScalingMethod::Mako;
    expect(ls::spatialScalingRequested(nativeEngine),
        "A selected scaler must activate when the engine is enabled");
    expect(ls::effectiveScalingMethod(nativeEngine) ==
            ls::ScalingMethod::Mako,
        "An ordinary scaling profile must retain its selected method");
    nativeEngine.ultra_performance = true;
    expect(!ls::effectiveAllowFp16({.allow_fp16 = false}, nativeEngine),
        "Ultra Performance must respect an explicit FP32 choice");
    expect(ls::effectiveAllowFp16({.allow_fp16 = true}, nativeEngine),
        "Ultra Performance must retain an explicit FP16 choice");
    expect(ls::effectiveScalingMethod(nativeEngine) ==
            ls::ScalingMethod::Ls1Performance,
        "Ultra Performance must select LS1 Performance for an enabled engine");
    nativeEngine.scaling_enabled = false;
    expect(ls::effectiveScalingMethod(nativeEngine) ==
            ls::ScalingMethod::Mako,
        "Ultra Performance must not activate or overwrite a disabled scaler");

    const auto directory = std::filesystem::temp_directory_path() /
        ("mako-config-test-" + std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::create_directories(directory);

    unsetenv("MAKO_ENV");
    testOptionalLosslessScaling(directory);
    testPowerProfiles(directory);

    const auto defaultPath = directory / "default.toml";
    ls::ConfigFile::createDefaultConfigFile(defaultPath);
    const ls::ConfigFile generatedDefaults(defaultPath);
    const ls::ConfigFile inMemoryDefaults;
    expect(ls::GlobalConf{}.allow_fp16 &&
            generatedDefaults.global().allow_fp16 &&
            inMemoryDefaults.global().allow_fp16,
        "Renderer defaults must allow FP16 on supported hardware");
    expect(generatedDefaults.global().dll == inMemoryDefaults.global().dll &&
            generatedDefaults.global().allow_fp16 ==
                inMemoryDefaults.global().allow_fp16 &&
            std::ranges::equal(
                generatedDefaults.profiles(), inMemoryDefaults.profiles(),
                sameGameConf
            ),
        "The documented default TOML and in-memory examples must stay equivalent");

    const auto precisionPath = directory / "precision.toml";
    writeText(precisionPath, "version = 2\n[[profile]]\nname = 'hdr'\n");
    expect(ls::ConfigFile(precisionPath).profiles().front().hdr_reduced_precision,
        "Profiles without a saved HDR precision choice must default to reduced precision");
    writeText(precisionPath, "version = 2\n[[profile]]\nname = 'hdr'\nhdr_reduced_precision = false\n");
    ls::ConfigFile(precisionPath).write(precisionPath);
    expect(!ls::ConfigFile(precisionPath).profiles().front().hdr_reduced_precision,
        "An explicit full HDR precision choice must survive saving and reloading");
    writeText(precisionPath, "version = 2\n[[profile]]\nname = 'hdr'\nhdr_reduced_precision = true\n[profile.handheld]\nhdr_reduced_precision = false\n[profile.docked]\nhdr_reduced_precision = true\n");
    auto hdrPrecisionConfig = ls::ConfigFile(precisionPath);
    hdrPrecisionConfig.write(precisionPath);
    hdrPrecisionConfig = ls::ConfigFile(precisionPath);
    const auto& hdrPrecisionProfile = hdrPrecisionConfig.profiles().front();
    expect(hdrPrecisionProfile.hdr_reduced_precision &&
            !ls::profileForPowerSource(hdrPrecisionProfile, ls::PowerSource::Handheld).hdr_reduced_precision &&
            ls::profileForPowerSource(hdrPrecisionProfile, ls::PowerSource::Docked).hdr_reduced_precision,
        "HDR precision must round-trip independently through base and power profiles");
    for (const std::string_view global : {"", "[global]\n", "[global]\nallow_fp16 = true\n"}) {
        writeText(precisionPath, "version = 2\n" + std::string(global) + "[[profile]]\n");
        expect(ls::ConfigFile(precisionPath).global().allow_fp16,
            "Omitting the global section or FP16 setting must still allow FP16");
    }
    writeText(precisionPath, "version = 2\n[global]\nallow_fp16 = false\n[[profile]]\n");
    const ls::ConfigFile fp32Config(precisionPath);
    expect(!fp32Config.global().allow_fp16,
        "An explicit FP32 setting must override the FP16 default");
    fp32Config.write(precisionPath);
    expect(!ls::ConfigFile(precisionPath).global().allow_fp16,
        "Saving an existing FP32 configuration must preserve its precision");

    const auto path = directory / "conf.toml";
    writeText(path, validConfiguration);

    unsetenv("MAKO_ENV");
    setenv("MAKO_CONFIG", path.c_str(), 1);

    ls::WatchedConfig config;
    expect(!config.update(),
        "The configuration parsed by the constructor must not reload immediately");

    const auto initialTimestamp = std::filesystem::last_write_time(path);
    writeText(path, "version = [partial");
    std::filesystem::last_write_time(path, initialTimestamp + std::chrono::seconds(2));

    bool firstParseFailed = false;
    try {
        static_cast<void>(config.update());
    } catch (const std::exception&) {
        firstParseFailed = true;
    }
    expect(firstParseFailed, "A partial configuration must be rejected");

    expect(!config.update(),
        "A failed parse must be briefly backed off instead of retried every frame");

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    bool sameVersionRetried = false;
    try {
        static_cast<void>(config.update());
    } catch (const std::exception&) {
        sameVersionRetried = true;
    }
    expect(sameVersionRetried,
        "A failed parse must retain its timestamp so the same version is retried");

    writeText(path, validConfiguration);
    std::filesystem::last_write_time(path, initialTimestamp + std::chrono::seconds(4));
    expect(config.update(), "A subsequent complete configuration must be accepted");
    expect(config.get().profiles().size() == 1,
        "The accepted configuration must replace the previous profile set");
    expect(config.get().profiles().front().target_fps == 144,
        "The accepted configuration must expose its new policy");
    expect(config.get().profiles().front().adaptive_target_refresh_rate,
        "The accepted configuration must expose automatic refresh targeting");
    expect(config.get().profiles().front().dynamic_cadence_recovery,
        "The accepted configuration must expose dynamic cadence recovery");
    expect(config.get().profiles().front().dynamic_cadence_probe_interval_seconds == 0.1F,
        "The accepted configuration must expose the cadence probe interval");
    expect(config.get().profiles().front().frame_generation_refresh_threshold == 60,
        "The accepted configuration must expose the refresh-rate threshold");
    expect(!config.get().profiles().front().frame_generation_provisioned &&
            config.get().profiles().front().scaling_enabled &&
            config.get().profiles().front()
                .swapchain_image_count_compatibility &&
            config.get().profiles().front().scaling_method ==
                ls::ScalingMethod::Ls1 &&
            config.get().profiles().front().scaling_factor == 1.75F &&
            config.get().profiles().front().scaling_sharpness == 0.6F,
        "The accepted configuration must expose the scaling policy");
    expect(config.get().profiles().front().ultra_performance &&
            config.get().profiles().front().flow_scale == 0.95F &&
            !config.get().profiles().front().performance_mode &&
            ls::effectiveFlowScale(config.get().profiles().front()) ==
                ls::GameConfDefaults::ultraPerformanceFlowScale &&
            ls::effectivePerformanceMode(config.get().profiles().front()),
        "Ultra Performance must preserve saved settings while overriding backend construction");
    expect(config.get().profiles().front().base_fps_cap == 0 &&
            !config.get().profiles().front().adaptive_auto_base_fps_cap &&
            config.get().profiles().front()
                    .adaptive_fractional_real_frame_priority ==
                ls::AdaptiveFractionalRealFramePriority::Auto,
        "Dynamic cadence recovery must disable every base FPS cap");
    expect(config.get().profiles().front().multiplier == 2,
        "Unknown legacy options must be inert without disturbing known defaults");

    const auto canonicalPath = directory / "canonical.toml";
    config.get().write(canonicalPath);
    const auto canonicalConfiguration = readText(canonicalPath);
    expect(canonicalConfiguration.find("removed_global_option") == std::string::npos &&
            canonicalConfiguration.find("removed_profile_option") == std::string::npos,
        "A canonical Renderer write must remove unknown legacy options");
    const ls::ConfigFile canonicalConfig(canonicalPath);
    expect(std::ranges::equal(
            config.get().profiles(), canonicalConfig.profiles(), sameGameConf
        ),
        "A canonical Renderer write must preserve scaling configuration");

    // Real short writes must preserve the previous file, including when the
    // process has permission to open it. Exercise every configuration writer.
    for (const int writer : {0, 1, 2}) {
        const auto failurePath = directory / "write-failure.conf";
        writeText(failurePath, "previous configuration\n");
        struct rlimit previousLimit {};
        expect(::getrlimit(RLIMIT_FSIZE, &previousLimit) == 0,
            "Could not read file-size limit for write failure test");
        auto limited = previousLimit;
        limited.rlim_cur = 16;
        const auto previousSignal = std::signal(SIGXFSZ, SIG_IGN);
        expect(::setrlimit(RLIMIT_FSIZE, &limited) == 0,
            "Could not constrain file writes for failure test");
        bool failed = false;
        try {
            if (writer == 0) canonicalConfig.write(failurePath);
            else if (writer == 1) ls::ConfigFile::createDefaultConfigFile(failurePath);
            else ls::LaunchConfigFile{}.write(failurePath);
        } catch (const std::exception&) {
            failed = true;
        }
        const auto restored = ::setrlimit(RLIMIT_FSIZE, &previousLimit);
        std::signal(SIGXFSZ, previousSignal);
        expect(restored == 0, "Could not restore file-size limit");
        expect(failed, "Configuration writer silently accepted a short write");
        expect(readText(failurePath) == "previous configuration\n",
            "A failed configuration write damaged the previous file");
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            expect(!entry.path().filename().string().starts_with(".write-failure.conf."),
                "A failed configuration write left a temporary file");
        }
    }

    const auto linkedPath = directory / "linked.toml";
    std::filesystem::create_symlink(canonicalPath, linkedPath);
    canonicalConfig.write(linkedPath);
    expect(std::filesystem::is_symlink(linkedPath) &&
            readText(canonicalPath) == canonicalConfiguration,
        "Atomic configuration writes must preserve configured symlinks");
    const auto missingTarget = directory / "new-target.toml";
    const auto danglingPath = directory / "new-link.toml";
    std::filesystem::create_symlink(missingTarget.filename(), danglingPath);
    canonicalConfig.write(danglingPath);
    expect(std::filesystem::is_symlink(danglingPath) &&
            readText(missingTarget) == canonicalConfiguration,
        "First-time configuration writes must preserve dangling relative symlinks");
    std::filesystem::permissions(canonicalPath, std::filesystem::perms::owner_read);
    bool readOnlyRejected = false;
    try {
        canonicalConfig.write(canonicalPath);
    } catch (const std::exception&) {
        readOnlyRejected = true;
    }
    expect(readOnlyRejected && readText(canonicalPath) == canonicalConfiguration,
        "A read-only configuration must be preserved without changing permissions");
    std::filesystem::permissions(canonicalPath,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);

    const auto fixedMultiplierPath = directory / "fixed-multiplier.toml";
    writeText(fixedMultiplierPath, R"(version = 2
[[profile]]
multiplier = 5
)");
    const ls::ConfigFile fixedMultiplierConfiguration(fixedMultiplierPath);
    expect(fixedMultiplierConfiguration.profiles().front().multiplier == 5,
        "The fixed multiplier must accept the supported 5x maximum");

    const auto invalidFixedMultiplierPath = directory / "invalid-fixed-multiplier.toml";
    writeText(invalidFixedMultiplierPath, R"(version = 2
[[profile]]
multiplier = 6
)");
    bool invalidFixedMultiplierRejected = false;
    try {
        static_cast<void>(ls::ConfigFile(invalidFixedMultiplierPath));
    } catch (const std::exception&) {
        invalidFixedMultiplierRejected = true;
    }
    expect(invalidFixedMultiplierRejected,
        "Fixed multipliers above the supported 5x maximum must be rejected");

    const auto priorityPath = directory / "fractional-priority.toml";
    writeText(priorityPath, R"(version = 2
[[profile]]
adaptive = true
adaptive_auto_base_fps_cap = false
adaptive_fractional_real_frame_priority = "very-high"
)");
    const ls::ConfigFile priorityConfiguration(priorityPath);
    expect(priorityConfiguration.profiles().front()
                .adaptive_fractional_real_frame_priority ==
            ls::AdaptiveFractionalRealFramePriority::VeryHigh,
        "Fractional real-frame priority did not preserve its stable token");

    const auto invalidPriorityPath = directory / "invalid-fractional-priority.toml";
    writeText(invalidPriorityPath, R"(version = 2
[[profile]]
adaptive_fractional_real_frame_priority = "maximum"
)");
    bool invalidPriorityRejected = false;
    try {
        static_cast<void>(ls::ConfigFile(invalidPriorityPath));
    } catch (const std::exception&) {
        invalidPriorityRejected = true;
    }
    expect(invalidPriorityRejected,
        "Unknown Fractional real-frame priorities must be rejected");

    const auto vrrPath = directory / "gamescope-vrr.toml";
    writeText(vrrPath, R"(version = 2
[[profile]]
gamescope_vrr_mode = "off"
)");
    const ls::ConfigFile vrrConfiguration(vrrPath);
    expect(vrrConfiguration.profiles().front().gamescope_vrr_mode ==
            ls::GamescopeVrrMode::Off,
        "Gamescope VRR mode did not preserve the off token");
    const auto invalidVrrPath = directory / "invalid-gamescope-vrr.toml";
    writeText(invalidVrrPath, R"(version = 2
[[profile]]
gamescope_vrr_mode = "automatic"
)");
    bool invalidVrrRejected = false;
    try {
        static_cast<void>(ls::ConfigFile(invalidVrrPath));
    } catch (const std::exception&) {
        invalidVrrRejected = true;
    }
    expect(invalidVrrRejected, "Unknown Gamescope VRR modes must be rejected");

    for (const std::string_view invalidInterval : {"0.09", "4"}) {
        const auto invalidIntervalPath = directory /
            ("invalid-probe-interval-" + std::string(invalidInterval) +
             ".toml");
        writeText(invalidIntervalPath,
            "version = 2\n[[profile]]\n"
            "dynamic_cadence_probe_interval_seconds = " +
            std::string(invalidInterval) + "\n");
        bool invalidIntervalRejected = false;
        try {
            static_cast<void>(ls::ConfigFile(invalidIntervalPath));
        } catch (const std::exception&) {
            invalidIntervalRejected = true;
        }
        expect(invalidIntervalRejected,
            "Cadence probe intervals outside 0.1-3 seconds must be rejected");
    }

    for (const auto& [field, value] : {
            std::pair{"scaling_factor", "0.99"},
            std::pair{"scaling_factor", "2.01"},
            std::pair{"scaling_factor", "nan"},
            std::pair{"scaling_sharpness", "-0.01"},
            std::pair{"scaling_sharpness", "1.01"},
            std::pair{"scaling_sharpness", "inf"},
        }) {
        const auto invalidScalingPath = directory /
            ("invalid-" + std::string(field) + '-' + value + ".toml");
        writeText(invalidScalingPath,
            "version = 2\n[[profile]]\n" + std::string(field) + " = " +
            value + "\n");
        bool invalidScalingRejected = false;
        try {
            static_cast<void>(ls::ConfigFile(invalidScalingPath));
        } catch (const std::exception&) {
            invalidScalingRejected = true;
        }
        expect(invalidScalingRejected,
            "Scaling values outside their public ranges must be rejected");
    }

    const auto scalingOnlyPath = directory / "scaling-only.toml";
    writeText(scalingOnlyPath, R"(version = 2
[[profile]]
frame_generation_provisioned = false
frame_generation_enabled = false
scaling_enabled = true
scaling_factor = 1.5
scaling_sharpness = 0.5
)");
    const ls::ConfigFile scalingOnlyConfiguration(scalingOnlyPath);
    const auto& scalingOnlyProfile =
        scalingOnlyConfiguration.profiles().front();
    expect(!scalingOnlyProfile.frame_generation_provisioned &&
            !scalingOnlyProfile.frame_generation_enabled &&
            scalingOnlyProfile.scaling_enabled &&
            scalingOnlyProfile.scaling_factor == 1.5F &&
            scalingOnlyProfile.scaling_sharpness == 0.5F,
        "Scaling configuration must remain independent of frame generation");

    ls::ConfigFile detectionConfig;
    detectionConfig.profiles() = {
        ls::GameConf{.name = "mako"},
        ls::GameConf{.name = "captured", .active_in = {"CoolGame.exe"}},
    };

    setenv("MAKO_PROFILE", "captured", 1);
    const auto environmentIdentification = ls::identify();
    expect(environmentIdentification.override.has_value() &&
            environmentIdentification.override.value() == "captured",
        "MAKO_PROFILE must populate the explicit profile override");
    unsetenv("MAKO_PROFILE");
    setenv("MAKO_FOLLOW_CURRENT_PROFILE", "1", 1);
    expect(ls::identify().follow_current_profile,
        "The streaming launcher must opt in to the saved selection");
    setenv("MAKO_FOLLOW_CURRENT_PROFILE", "0", 1);
    expect(!ls::identify().follow_current_profile,
        "A disabled selection-following flag must preserve normal matching");
    unsetenv("MAKO_FOLLOW_CURRENT_PROFILE");

    ls::Identification identification{
        .fallback = "mako",
        .executable = "/games/CoolGame.exe",
        .process_name = "CoolGame.exe",
    };
    auto detectedProfile = ls::findProfile(detectionConfig, identification);
    expect(detectedProfile.has_value() &&
            detectedProfile->first == ls::IdentType::EXECUTABLE &&
            detectedProfile->second.name == "captured",
        "A captured process profile must supersede the launch-time fallback");

    identification.executable = "/games/UnknownGame";
    identification.process_name = "UnknownGame";
    detectedProfile = ls::findProfile(detectionConfig, identification);
    expect(detectedProfile.has_value() &&
            detectedProfile->first == ls::IdentType::FALLBACK &&
            detectedProfile->second.name == "mako",
        "An unknown game must retain the default renderer context");

    identification.override = "mako";
    identification.executable = "/games/CoolGame.exe";
    identification.process_name = "CoolGame.exe";
    detectedProfile = ls::findProfile(detectionConfig, identification);
    expect(detectedProfile.has_value() &&
            detectedProfile->first == ls::IdentType::OVERRIDE &&
            detectedProfile->second.name == "mako",
        "An explicit caller profile must remain a hard override");

    detectionConfig.current_profile = "captured";
    identification.follow_current_profile = true;
    detectedProfile = ls::findProfile(detectionConfig, identification);
    expect(detectedProfile && detectedProfile->second.name == "captured",
        "Opted-in clients must follow the current selection over their startup override");
    detectionConfig.current_profile = "missing";
    detectedProfile = ls::findProfile(detectionConfig, identification);
    expect(detectedProfile && detectedProfile->second.name == "mako",
        "An unavailable current selection must retain the startup profile");
    detectionConfig.current_profile = "captured";
    identification.follow_current_profile = false;
    expect(ls::findProfile(detectionConfig, identification)->second.name == "mako",
        "Ordinary explicit launch profiles must ignore editor selection changes");

    const auto selectionPath = directory / "selection-following.toml";
    detectionConfig.current_profile = "mako";
    detectionConfig.write(selectionPath);
    std::optional<std::string> previousConfiguration;
    if (const auto* previous = std::getenv("MAKO_CONFIG"))
        previousConfiguration = previous;
    setenv("MAKO_CONFIG", selectionPath.c_str(), 1);
    ls::WatchedConfig selectionWatcher(directory / "unused-power-supplies");
    auto streamIdentity = identification;
    streamIdentity.follow_current_profile = true;
    const auto selectionTimestamp = std::filesystem::last_write_time(selectionPath);
    detectionConfig.current_profile = "captured";
    detectionConfig.write(selectionPath);
    std::filesystem::last_write_time(selectionPath, selectionTimestamp + std::chrono::seconds(1));
    expect(selectionWatcher.update() &&
            ls::findProfile(selectionWatcher.get(), streamIdentity)->second.name == "captured",
        "The existing watcher must follow selection-only edits with unchanged profile settings");
    if (previousConfiguration)
        setenv("MAKO_CONFIG", previousConfiguration->c_str(), 1);
    else
        unsetenv("MAKO_CONFIG");

    // Launchers inherit exactly the same profile environment as their game.
    // Neither that environment nor an older captured launcher alias may make
    // MAKO change the launcher's Vulkan device or presentation resources.
    const auto gameIdentification = identification;
    std::vector<std::string> launchers;
    for (const auto name : ls::detail::excludedWindowsLauncherExecutables) {
        launchers.emplace_back(name);
        auto upper = std::string(name);
        std::transform(upper.begin(), upper.end(), upper.begin(), [](const char c) {
            return c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
        });
        launchers.push_back(std::move(upper));
    }
    for (const auto& launcher : launchers) {
        detectionConfig.profiles()[1].active_in.emplace_back(launcher);
        const ls::Identification launcherIdentification{
            .override = "captured",
            .fallback = "mako",
            .executable = "/proton/files/bin/wine64-preloader",
            .wine_executable = std::string("C:\\Launcher\\") + launcher,
            .process_name = "GameThread",
            .follow_current_profile = true,
        };
        expect(!ls::findProfile(detectionConfig, launcherIdentification),
            "An excluded launcher must stay native despite an inherited override");
        auto fallbackLauncher = launcherIdentification;
        fallbackLauncher.override.reset();
        expect(!ls::findProfile(detectionConfig, fallbackLauncher),
            "Captured launcher aliases and the default fallback must not activate MAKO");
        auto fallbackOnlyConfig = detectionConfig;
        fallbackOnlyConfig.profiles()[1].active_in.clear();
        expect(!ls::findProfile(fallbackOnlyConfig, fallbackLauncher),
            "An uncaptured launcher must not activate through the default fallback");
        auto matchedLauncher = fallbackLauncher;
        matchedLauncher.fallback.reset();
        expect(!ls::findProfile(detectionConfig, matchedLauncher),
            "An explicit launcher alias alone must not activate MAKO");
        auto directLauncher = launcherIdentification;
        directLauncher.wine_executable.reset();
        directLauncher.executable = std::string("/Launcher/") + launcher;
        expect(!ls::findProfile(detectionConfig, directLauncher),
            "An exact launcher executable must stay native without a Wine path");
        setenv("MAKO_ENV", "1", 1);
        expect(!ls::findProfile(detectionConfig, launcherIdentification),
            "Environment-only profiles must not activate MAKO in a launcher");
        expect(std::getenv("MAKO_ENV") &&
                std::string_view(std::getenv("MAKO_ENV")) == "1",
            "Launcher exclusion must not strip the child's inherited activation");
        expect(ls::findProfile(detectionConfig, gameIdentification).has_value(),
            "Skipping a launcher must leave its child's environment profile active");
        unsetenv("MAKO_ENV");
    }
    for (const auto* executable : {
            "/Ubisoft/UbisoftConnect.exe/TheCrewMotorfest.exe",
            "/games/MyUbisoftConnect.exe", "/games/upc.exe.backup",
            "/games/UplayWebCoreGame.exe", "/games/UnknownGame",
            "/CDPR/REDlauncher.exe/witcher3.exe", "/games/witcher3.exe",
            "/games/MyREDlauncher.exe", "/games/REDprelauncher.exe.backup",
            "/Rockstar Games/Launcher/MaxPayne3.exe", "/games/PlayMaxPayne3.exe",
            "/games/GTA5.exe", "/games/RDR2.exe", "/games/Launcher.exe",
            "/games/MyRockstarService.exe", "/games/EADesktop.exe.backup",
        }) {
        auto game = gameIdentification;
        game.executable = "/proton/files/bin/wine64-preloader";
        game.wine_executable = executable;
        game.process_name = "upc.exe"; // A thread name is not executable proof.
        const auto childProfile = ls::findProfile(detectionConfig, game);
        expect(childProfile && childProfile->second.name == "mako",
            "Launcher directories, partial names, and inherited thread names must not block a game");
    }

    setenv("MAKO_ENV", "1", 1);
    setenv("MAKO_ADAPTIVE", "0", 1);
    unsetenv("MAKO_HDR_REDUCED_PRECISION");
    expect(ls::WatchedConfig{}.get().profiles().front().hdr_reduced_precision,
        "Environment-only profiles must default to reduced HDR precision");
    setenv("MAKO_HDR_REDUCED_PRECISION", "0", 1);
    expect(!ls::WatchedConfig{}.get().profiles().front().hdr_reduced_precision,
        "MAKO_HDR_REDUCED_PRECISION=0 must preserve the full precision override");
    unsetenv("MAKO_HDR_REDUCED_PRECISION");
    unsetenv("MAKO_NO_FP16");
    expect(ls::WatchedConfig{}.get().global().allow_fp16,
        "Environment-only configuration must allow FP16 by default");
    setenv("MAKO_NO_FP16", "1", 1);
    expect(!ls::WatchedConfig{}.get().global().allow_fp16,
        "MAKO_NO_FP16=1 must preserve the explicit FP32 override");
    setenv("MAKO_NO_FP16", "0", 1);
    expect(ls::WatchedConfig{}.get().global().allow_fp16,
        "MAKO_NO_FP16=0 must allow FP16");
    unsetenv("MAKO_NO_FP16");
    setenv("MAKO_BASE_FPS_CAP", "30", 1);
    setenv("MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP", "1", 1);
    setenv("MAKO_ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY", "high", 1);
    setenv("MAKO_ADAPTIVE_TARGET_REFRESH_RATE", "1", 1);
    setenv("MAKO_DYNAMIC_CADENCE_RECOVERY", "1", 1);
    setenv("MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS", "0.5", 1);
    setenv("MAKO_FRAME_GENERATION_REFRESH_THRESHOLD", "130", 1);
    setenv("MAKO_FRAME_GENERATION_PROVISIONED", "0", 1);
    setenv("MAKO_ULTRA_PERFORMANCE", "1", 1);
    setenv("MAKO_SCALING_ENABLED", "1", 1);
    setenv("MAKO_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY", "1", 1);
    setenv("MAKO_SCALING_METHOD", "native", 1);
    setenv("MAKO_SCALING_FACTOR", "2", 1);
    setenv("MAKO_SCALING_SUPERSAMPLING", "1", 1);
    setenv("MAKO_SCALING_SHARPNESS", "0.75", 1);
    const ls::WatchedConfig environmentConfig;
    expect(environmentConfig.get().profiles().front().adaptive_target_refresh_rate,
        "Environment profiles must accept automatic refresh targeting");
    setenv("MAKO_ADAPTIVE_TARGET_REFRESH_RATE", "0", 1);
    expect(!ls::WatchedConfig{}.get().profiles().front().adaptive_target_refresh_rate,
        "Environment profiles must accept explicitly disabled refresh targeting");
    expect(environmentConfig.get().profiles().front().dynamic_cadence_recovery &&
            environmentConfig.get().profiles().front()
                .dynamic_cadence_probe_interval_seconds == 0.5F &&
            environmentConfig.get().profiles().front().frame_generation_refresh_threshold ==
                130 &&
            !environmentConfig.get().profiles().front()
                .frame_generation_provisioned &&
            environmentConfig.get().profiles().front().base_fps_cap == 0 &&
            !environmentConfig.get().profiles().front().adaptive_auto_base_fps_cap &&
            environmentConfig.get().profiles().front()
                    .adaptive_fractional_real_frame_priority ==
                ls::AdaptiveFractionalRealFramePriority::Auto &&
            environmentConfig.get().profiles().front().ultra_performance &&
            environmentConfig.get().profiles().front().scaling_enabled &&
            environmentConfig.get().profiles().front()
                .swapchain_image_count_compatibility &&
            environmentConfig.get().profiles().front().scaling_method ==
                ls::ScalingMethod::Native &&
            ls::spatialScalingRequested(
                environmentConfig.get().profiles().front()) &&
            environmentConfig.get().profiles().front().scaling_factor == 2.0F &&
            environmentConfig.get().profiles().front().scaling_supersampling &&
            environmentConfig.get().profiles().front().scaling_sharpness == 0.75F,
        "Environment configuration must expose scaling and cadence policy");
    unsetenv("MAKO_DYNAMIC_CADENCE_RECOVERY");
    unsetenv("MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS");
    unsetenv("MAKO_FRAME_GENERATION_REFRESH_THRESHOLD");
    unsetenv("MAKO_FRAME_GENERATION_PROVISIONED");
    unsetenv("MAKO_ULTRA_PERFORMANCE");
    unsetenv("MAKO_SCALING_ENABLED");
    unsetenv("MAKO_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY");
    unsetenv("MAKO_SCALING_METHOD");
    unsetenv("MAKO_SCALING_FACTOR");
    unsetenv("MAKO_SCALING_SUPERSAMPLING");
    unsetenv("MAKO_SCALING_SHARPNESS");
    unsetenv("MAKO_ADAPTIVE");
    unsetenv("MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP");
    unsetenv("MAKO_ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY");
    unsetenv("MAKO_ADAPTIVE_TARGET_REFRESH_RATE");
    unsetenv("MAKO_BASE_FPS_CAP");
    unsetenv("MAKO_ENV");

    std::filesystem::remove_all(directory);
    std::cout << "configuration watcher tests passed\n";
    return 0;
}
