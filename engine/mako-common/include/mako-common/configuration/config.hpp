/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ls {

    /// global configuration
    struct GlobalConf {
        /// optional dll override
        std::optional<std::string> dll;
        /// should fp16 be allowed
        bool allow_fp16{};
    };

    /// pacing methods
    enum class Pacing : uint8_t {
        /// do not perform any pacing (vsync+novrr)
        None
    };

    /// Renderer defaults used when a game profile omits fields
    struct GameConfDefaults {
        static constexpr size_t multiplier = 2;
        static constexpr bool frameGenerationEnabled = true;
        static constexpr uint32_t frameGenerationRefreshThreshold = 0;
        static constexpr uint32_t baseFpsCap = 0;
        static constexpr bool adaptive = false;
        static constexpr bool adaptiveAutoBaseFpsCap = false;
        static constexpr uint32_t targetFps = 120;
        static constexpr size_t adaptiveMaxMultiplier = 3;
        static constexpr bool adaptiveStableCadence = false;
        static constexpr bool dynamicCadenceRecovery = false;
        static constexpr float dynamicCadenceProbeIntervalSeconds = 2.0F;
        static constexpr bool ultraPerformance = false;
        static constexpr float ultraPerformanceFlowScale = 0.7F;
        static constexpr float flowScale = 1.0F;
        static constexpr bool performanceMode = false;
        static constexpr Pacing pacing = Pacing::None;
    };

    /// ranges accepted by the Renderer configuration parser
    struct GameConfLimits {
        static constexpr size_t minimumMultiplier = 2;
        static constexpr uint32_t minimumBaseFpsCap = 0;
        static constexpr uint32_t maximumBaseFpsCap = 1000;
        static constexpr uint32_t minimumFrameGenerationRefreshThreshold = 0;
        static constexpr uint32_t maximumFrameGenerationRefreshThreshold = 1000;
        static constexpr uint32_t minimumTargetFps = 10;
        static constexpr uint32_t maximumTargetFps = 1000;
        static constexpr size_t minimumAdaptiveMaxMultiplier = 2;
        static constexpr size_t maximumAdaptiveMaxMultiplier = 4;
        static constexpr float minimumDynamicCadenceProbeIntervalSeconds = 0.25F;
        static constexpr float maximumDynamicCadenceProbeIntervalSeconds = 3.0F;
        static constexpr float minimumFlowScale = 0.25F;
        static constexpr float maximumFlowScale = 1.0F;
    };

    /// game profile configuration
    struct GameConf {
        /// name of the profile
        std::string name{"Profile"};
        /// optional activation string/array
        std::vector<std::string> active_in;
        /// gpu to use (in case of multiple)
        std::optional<std::string> gpu;
        /// multiplier for frame generation
        size_t multiplier{GameConfDefaults::multiplier};
        /// allow frame synthesis to be toggled live without changing its mode
        bool frame_generation_enabled{GameConfDefaults::frameGenerationEnabled};
        /// pause synthesis at or below a confirmed refresh rate; zero disables it
        uint32_t frame_generation_refresh_threshold{
            GameConfDefaults::frameGenerationRefreshThreshold
        };
        /// maximum application-present rate before frame generation; zero disables it
        uint32_t base_fps_cap{GameConfDefaults::baseFpsCap};
        /// dynamically vary the generated-frame count toward a target framerate
        bool adaptive{GameConfDefaults::adaptive};
        /// cap Adaptive's real-frame input to half its target for even 2x cadence
        bool adaptive_auto_base_fps_cap{GameConfDefaults::adaptiveAutoBaseFpsCap};
        /// desired displayed framerate when adaptive mode is enabled
        uint32_t target_fps{GameConfDefaults::targetFps};
        /// maximum total multiplier Adaptive may use
        size_t adaptive_max_multiplier{GameConfDefaults::adaptiveMaxMultiplier};
        /// prefer a validated constant interpolation cadence when safe
        bool adaptive_stable_cadence{GameConfDefaults::adaptiveStableCadence};
        /// periodically expose native cadence to detect upward rate changes
        bool dynamic_cadence_recovery{GameConfDefaults::dynamicCadenceRecovery};
        /// seconds between optional native-cadence probes
        float dynamic_cadence_probe_interval_seconds{
            GameConfDefaults::dynamicCadenceProbeIntervalSeconds
        };
        /// trade live profile reloads for the lowest supported resource cost
        bool ultra_performance{GameConfDefaults::ultraPerformance};
        /// non-inverted flow scale
        float flow_scale{GameConfDefaults::flowScale};
        /// use performance mode
        bool performance_mode{GameConfDefaults::performanceMode};
        /// pacing method
        Pacing pacing{GameConfDefaults::pacing};
    };

    /// Flow scale selected for backend construction after applying presets.
    [[nodiscard]] constexpr float effectiveFlowScale(
            const GameConf& profile) noexcept {
        return profile.ultra_performance
            ? GameConfDefaults::ultraPerformanceFlowScale
            : profile.flow_scale;
    }

    /// Model selected for backend construction after applying presets.
    [[nodiscard]] constexpr bool effectivePerformanceMode(
            const GameConf& profile) noexcept {
        return profile.ultra_performance || profile.performance_mode;
    }

    /// FP16 permission selected for a process after applying its profile preset.
    [[nodiscard]] constexpr bool effectiveAllowFp16(
            const GlobalConf& global, const GameConf& profile) noexcept {
        return profile.ultra_performance || global.allow_fp16;
    }

    /// Scheduler duration represented by the public fractional-seconds value.
    [[nodiscard]] constexpr std::chrono::milliseconds
    dynamicCadenceProbeIntervalDuration(const float seconds) noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<float>(seconds)
        );
    }

    /// parsed configuration file
    class ConfigFile {
    public:
        static constexpr int64_t formatVersion = 2;

        /// create a default configuration file at the given path
        /// @param path path to configuration file
        /// @throws ls::error on failure
        static void createDefaultConfigFile(const std::filesystem::path& path);

        /// load the default configuration
        /// @throws ls::error on failure
        ConfigFile();
        /// load configuration from file
        /// @param path path to configuration file
        /// @throws ls::error on failure
        ConfigFile(const std::filesystem::path& path);

        /// get the global configuration
        /// @return global configuration
        [[nodiscard]] auto& global() { return this->globalConf; }
        /// get the game profiles
        /// @return list of game profiles
        [[nodiscard]] auto& profiles() { return this->profileConfs; }

        /// get the global configuration
        /// @return global configuration
        [[nodiscard]] const auto& global() const { return this->globalConf; }
        /// get the game profiles
        /// @return list of game profiles
        [[nodiscard]] const auto& profiles() const { return this->profileConfs; }

        /// write the configuration back to file
        /// @param path path to configuration file
        /// @throws ls::error on failure
        void write(const std::filesystem::path& path) const;
    private:
        GlobalConf globalConf{};
        std::vector<GameConf> profileConfs;
    };

    /// configuration watcher with additional environment support
    class WatchedConfig {
    public:
        /// create a new configuration watcher
        /// @throws ls::error on failure
        WatchedConfig();

        /// reload the configuration from disk if it has changed
        /// @throws ls::error on failure
        /// @return true if the configuration was reloaded
        bool update();

        /// access the underlying configuration file
        /// @return configuration file
        [[nodiscard]] const auto& get() const { return this->configFile; }
    private:
        ConfigFile configFile;

        std::filesystem::path path;
        std::chrono::time_point<std::chrono::file_clock> last_timestamp;
        std::optional<std::chrono::time_point<std::chrono::file_clock>>
            failed_timestamp;
        std::chrono::steady_clock::time_point next_parse_retry;
    };

    /// find the configuration file in the most common locations
    /// @return path to configuration file
    std::filesystem::path findConfigurationFile();

}
