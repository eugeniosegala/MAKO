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
frame_generation_refresh_threshold = 60
base_fps_cap = 60
adaptive_auto_base_fps_cap = true
target_fps = 144
adaptive_max_multiplier = 4
dynamic_cadence_recovery = true
)";
}

int main() {
    const ls::GameConf defaults;
    expect(defaults.multiplier == ls::GameConfDefaults::multiplier &&
            defaults.frame_generation_enabled ==
                ls::GameConfDefaults::frameGenerationEnabled &&
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
    expect(config.get().profiles().front().frame_generation_refresh_threshold == 60,
        "The accepted configuration must expose the refresh-rate threshold");
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

    const auto fixedMultiplierPath = directory / "fixed-multiplier.toml";
    writeText(fixedMultiplierPath, R"(version = 2
[[profile]]
multiplier = 5
)");
    const ls::ConfigFile fixedMultiplierConfiguration(fixedMultiplierPath);
    expect(fixedMultiplierConfiguration.profiles().front().multiplier == 5,
        "The fixed multiplier must retain its established open upper range");

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
    setenv("MAKO_FRAME_GENERATION_REFRESH_THRESHOLD", "130", 1);
    const ls::WatchedConfig environmentConfig;
    expect(environmentConfig.get().profiles().front().dynamic_cadence_recovery &&
            environmentConfig.get().profiles().front().frame_generation_refresh_threshold ==
                130 &&
            environmentConfig.get().profiles().front().base_fps_cap == 0 &&
            !environmentConfig.get().profiles().front().adaptive_auto_base_fps_cap,
        "Environment dynamic cadence recovery must disable both base FPS caps");
    unsetenv("MAKO_DYNAMIC_CADENCE_RECOVERY");
    unsetenv("MAKO_FRAME_GENERATION_REFRESH_THRESHOLD");
    unsetenv("MAKO_ADAPTIVE");
    unsetenv("MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP");
    unsetenv("MAKO_BASE_FPS_CAP");
    unsetenv("MAKO_ENV");

    std::filesystem::remove_all(directory);
    std::cout << "configuration watcher tests passed\n";
    return 0;
}
