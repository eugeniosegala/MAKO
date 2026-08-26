/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/detection.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <unistd.h>

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
            left.frame_generation_enabled == right.frame_generation_enabled &&
            left.scaling_enabled == right.scaling_enabled &&
            left.scaling_method == right.scaling_method &&
            left.scaling_factor == right.scaling_factor &&
            left.scaling_sharpness == right.scaling_sharpness &&
            left.frame_generation_refresh_threshold ==
                right.frame_generation_refresh_threshold &&
            left.base_fps_cap == right.base_fps_cap &&
            left.adaptive == right.adaptive &&
            left.adaptive_auto_base_fps_cap ==
                right.adaptive_auto_base_fps_cap &&
            left.target_fps == right.target_fps &&
            left.adaptive_max_multiplier == right.adaptive_max_multiplier &&
            left.adaptive_stable_cadence == right.adaptive_stable_cadence &&
            left.dynamic_cadence_recovery == right.dynamic_cadence_recovery &&
            left.dynamic_cadence_probe_interval_seconds ==
                right.dynamic_cadence_probe_interval_seconds &&
            left.ultra_performance == right.ultra_performance &&
            left.flow_scale == right.flow_scale &&
            left.performance_mode == right.performance_mode &&
            left.pacing == right.pacing;
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
scaling_enabled = true
scaling_method = "ls1"
scaling_factor = 1.75
scaling_sharpness = 0.6
frame_generation_refresh_threshold = 60
base_fps_cap = 60
adaptive_auto_base_fps_cap = true
target_fps = 144
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
    expect(defaults.multiplier == ls::GameConfDefaults::multiplier &&
            defaults.frame_generation_enabled ==
                ls::GameConfDefaults::frameGenerationEnabled &&
            defaults.scaling_enabled ==
                ls::GameConfDefaults::scalingEnabled &&
            defaults.scaling_method ==
                ls::GameConfDefaults::scalingMethod &&
            defaults.scaling_factor == ls::GameConfDefaults::scalingFactor &&
            defaults.scaling_sharpness ==
                ls::GameConfDefaults::scalingSharpness &&
            defaults.frame_generation_refresh_threshold ==
                ls::GameConfDefaults::frameGenerationRefreshThreshold &&
            defaults.base_fps_cap == ls::GameConfDefaults::baseFpsCap &&
            defaults.adaptive == ls::GameConfDefaults::adaptive &&
            defaults.adaptive_auto_base_fps_cap ==
                ls::GameConfDefaults::adaptiveAutoBaseFpsCap &&
            defaults.target_fps == ls::GameConfDefaults::targetFps &&
            defaults.adaptive_max_multiplier ==
                ls::GameConfDefaults::adaptiveMaxMultiplier &&
            defaults.adaptive_stable_cadence ==
                ls::GameConfDefaults::adaptiveStableCadence &&
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

    const auto directory = std::filesystem::temp_directory_path() /
        ("mako-config-test-" + std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::create_directories(directory);

    const auto defaultPath = directory / "default.toml";
    ls::ConfigFile::createDefaultConfigFile(defaultPath);
    const ls::ConfigFile generatedDefaults(defaultPath);
    const ls::ConfigFile inMemoryDefaults;
    expect(generatedDefaults.global().dll == inMemoryDefaults.global().dll &&
            generatedDefaults.global().allow_fp16 ==
                inMemoryDefaults.global().allow_fp16 &&
            std::ranges::equal(
                generatedDefaults.profiles(), inMemoryDefaults.profiles(),
                sameGameConf
            ),
        "The documented default TOML and in-memory examples must stay equivalent");

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
    expect(config.get().profiles().front().dynamic_cadence_recovery,
        "The accepted configuration must expose dynamic cadence recovery");
    expect(config.get().profiles().front().dynamic_cadence_probe_interval_seconds == 0.1F,
        "The accepted configuration must expose the cadence probe interval");
    expect(config.get().profiles().front().frame_generation_refresh_threshold == 60,
        "The accepted configuration must expose the refresh-rate threshold");
    expect(config.get().profiles().front().scaling_enabled &&
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
            !config.get().profiles().front().adaptive_auto_base_fps_cap,
        "Dynamic cadence recovery must disable both base FPS caps");
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

    const auto fixedMultiplierPath = directory / "fixed-multiplier.toml";
    writeText(fixedMultiplierPath, R"(version = 2
[[profile]]
multiplier = 5
)");
    const ls::ConfigFile fixedMultiplierConfiguration(fixedMultiplierPath);
    expect(fixedMultiplierConfiguration.profiles().front().multiplier == 5,
        "The fixed multiplier must retain its established open upper range");

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
frame_generation_enabled = false
scaling_enabled = true
scaling_factor = 1.5
scaling_sharpness = 0.5
)");
    const ls::ConfigFile scalingOnlyConfiguration(scalingOnlyPath);
    const auto& scalingOnlyProfile =
        scalingOnlyConfiguration.profiles().front();
    expect(!scalingOnlyProfile.frame_generation_enabled &&
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

    setenv("MAKO_ENV", "1", 1);
    setenv("MAKO_ADAPTIVE", "0", 1);
    setenv("MAKO_BASE_FPS_CAP", "30", 1);
    setenv("MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP", "1", 1);
    setenv("MAKO_DYNAMIC_CADENCE_RECOVERY", "1", 1);
    setenv("MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS", "0.5", 1);
    setenv("MAKO_FRAME_GENERATION_REFRESH_THRESHOLD", "130", 1);
    setenv("MAKO_ULTRA_PERFORMANCE", "1", 1);
    setenv("MAKO_SCALING_ENABLED", "1", 1);
    setenv("MAKO_SCALING_FACTOR", "2", 1);
    setenv("MAKO_SCALING_SHARPNESS", "0.75", 1);
    const ls::WatchedConfig environmentConfig;
    expect(environmentConfig.get().profiles().front().dynamic_cadence_recovery &&
            environmentConfig.get().profiles().front()
                .dynamic_cadence_probe_interval_seconds == 0.5F &&
            environmentConfig.get().profiles().front().frame_generation_refresh_threshold ==
                130 &&
            environmentConfig.get().profiles().front().base_fps_cap == 0 &&
            !environmentConfig.get().profiles().front().adaptive_auto_base_fps_cap &&
            environmentConfig.get().profiles().front().ultra_performance &&
            environmentConfig.get().profiles().front().scaling_enabled &&
            environmentConfig.get().profiles().front().scaling_factor == 2.0F &&
            environmentConfig.get().profiles().front().scaling_sharpness == 0.75F,
        "Environment configuration must expose scaling and cadence policy");
    unsetenv("MAKO_DYNAMIC_CADENCE_RECOVERY");
    unsetenv("MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS");
    unsetenv("MAKO_FRAME_GENERATION_REFRESH_THRESHOLD");
    unsetenv("MAKO_ULTRA_PERFORMANCE");
    unsetenv("MAKO_SCALING_ENABLED");
    unsetenv("MAKO_SCALING_FACTOR");
    unsetenv("MAKO_SCALING_SHARPNESS");
    unsetenv("MAKO_ADAPTIVE");
    unsetenv("MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP");
    unsetenv("MAKO_BASE_FPS_CAP");
    unsetenv("MAKO_ENV");

    std::filesystem::remove_all(directory);
    std::cout << "configuration watcher tests passed\n";
    return 0;
}
