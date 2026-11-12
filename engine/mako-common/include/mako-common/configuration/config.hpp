/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ls {

    namespace detail { class PowerSourceMonitor; }

    /// global configuration
    struct GlobalConf {
        /// optional dll override
        std::optional<std::string> dll;
        /// request supported FP16 model/scaling arithmetic; false selects FP32
        bool allow_fp16{true};
    };

    /// Frame-generation backend selected at process startup.
    enum class FrameGenerationModel : uint8_t { LosslessScaling, MakoFg };

    [[nodiscard]] constexpr const char* frameGenerationModelName(
            FrameGenerationModel model) noexcept {
        return model == FrameGenerationModel::MakoFg ? "mako-fg" : "lossless-scaling";
    }

    [[nodiscard]] constexpr std::optional<FrameGenerationModel>
    frameGenerationModelFromName(std::string_view name) noexcept {
        if (name == "lossless-scaling") return FrameGenerationModel::LosslessScaling;
        if (name == "mako-fg") return FrameGenerationModel::MakoFg;
        return std::nullopt;
    }

    /// How often MAKO FG solves motion. Pairs between solved ones carry the last
    /// motion forward, so higher modes trade motion accuracy for GPU time.
    enum class MakoFgMode : uint8_t { Quality, Balanced, Performance, ExtraPerformance };

    [[nodiscard]] constexpr const char* makoFgModeName(MakoFgMode mode) noexcept {
        switch (mode) {
            case MakoFgMode::Quality: return "quality";
            case MakoFgMode::Performance: return "performance";
            case MakoFgMode::ExtraPerformance: return "extra-performance";
            case MakoFgMode::Balanced: break;
        }
        return "balanced";
    }

    [[nodiscard]] constexpr std::optional<MakoFgMode>
    makoFgModeFromName(std::string_view name) noexcept {
        if (name == "quality") return MakoFgMode::Quality;
        if (name == "balanced") return MakoFgMode::Balanced;
        if (name == "performance") return MakoFgMode::Performance;
        if (name == "extra-performance") return MakoFgMode::ExtraPerformance;
        return std::nullopt;
    }

    /// One real-frame pair in this many solves motion: 1, 2, 3 or 4.
    [[nodiscard]] constexpr int makoFgMotionPeriod(MakoFgMode mode) noexcept {
        return static_cast<int>(mode) + 1;
    }

    /// pacing methods
    enum class Pacing : uint8_t {
        /// do not perform any pacing (vsync+novrr)
        None
    };

    /// Live Gamescope VRR preference for a matched MAKO launch.
    enum class GamescopeVrrMode : uint8_t { FollowSteam, On, Off };

    [[nodiscard]] constexpr const char* gamescopeVrrModeName(
            GamescopeVrrMode mode) noexcept {
        switch (mode) {
            case GamescopeVrrMode::FollowSteam: return "follow-steam";
            case GamescopeVrrMode::On: return "on";
            case GamescopeVrrMode::Off: return "off";
        }
        return "follow-steam";
    }

    [[nodiscard]] constexpr std::optional<GamescopeVrrMode>
    gamescopeVrrModeFromName(std::string_view value) noexcept {
        if (value == "follow-steam") return GamescopeVrrMode::FollowSteam;
        if (value == "on") return GamescopeVrrMode::On;
        if (value == "off") return GamescopeVrrMode::Off;
        return std::nullopt;
    }

    /// HDR storage and frame-generation quality, independent of HDR activation.
    enum class HdrQuality : uint8_t { Quality, Balanced, Performance };

    [[nodiscard]] constexpr const char* hdrQualityName(HdrQuality quality) noexcept {
        switch (quality) {
            case HdrQuality::Quality: return "quality";
            case HdrQuality::Balanced: return "balanced";
            case HdrQuality::Performance: return "performance";
        }
        return "performance";
    }

    [[nodiscard]] constexpr std::optional<HdrQuality>
    hdrQualityFromName(std::string_view value) noexcept {
        if (value == "quality") return HdrQuality::Quality;
        if (value == "balanced") return HdrQuality::Balanced;
        if (value == "performance") return HdrQuality::Performance;
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool hdrCompactBuffers(HdrQuality quality) noexcept {
        return quality != HdrQuality::Quality;
    }

    [[nodiscard]] constexpr bool hdrApproximateGeneration(HdrQuality quality) noexcept {
        return quality == HdrQuality::Performance;
    }

    /// Spatial reconstruction implementation selected for a profile.
    enum class ScalingMethod : uint8_t {
        /// Preserve the game's native image while the Scaling Engine remains provisioned.
        Native,
        /// MAKO's open, single-pass edge-adaptive scaler.
        Mako,
        /// Lossless Scaling's multi-pass LS1 neural model.
        Ls1,
        /// Lossless Scaling's lower-cost LS1 neural model.
        Ls1Performance,
    };

    /// Optional Fractional Adaptive source cap selected from cadence-friendly
    /// real/generated ratios. Auto preserves the ordinary Fractional policy.
    enum class AdaptiveFractionalRealFramePriority : uint8_t {
        Auto,
        Low,
        Medium,
        High,
        VeryHigh,
    };

    [[nodiscard]] constexpr const char*
    adaptiveFractionalRealFramePriorityName(
            const AdaptiveFractionalRealFramePriority priority) noexcept {
        switch (priority) {
            case AdaptiveFractionalRealFramePriority::Auto:
                return "auto";
            case AdaptiveFractionalRealFramePriority::Low:
                return "low";
            case AdaptiveFractionalRealFramePriority::Medium:
                return "medium";
            case AdaptiveFractionalRealFramePriority::High:
                return "high";
            case AdaptiveFractionalRealFramePriority::VeryHigh:
                return "very-high";
        }
        return "auto";
    }

    [[nodiscard]] constexpr std::optional<
        AdaptiveFractionalRealFramePriority>
    adaptiveFractionalRealFramePriorityFromName(
            const std::string_view value) noexcept {
        if (value == "auto")
            return AdaptiveFractionalRealFramePriority::Auto;
        if (value == "low")
            return AdaptiveFractionalRealFramePriority::Low;
        if (value == "medium")
            return AdaptiveFractionalRealFramePriority::Medium;
        if (value == "high")
            return AdaptiveFractionalRealFramePriority::High;
        if (value == "very-high")
            return AdaptiveFractionalRealFramePriority::VeryHigh;
        return std::nullopt;
    }

    [[nodiscard]] constexpr double adaptiveFractionalRealFramePriorityCap(
            const AdaptiveFractionalRealFramePriority priority,
            const uint32_t targetFps) noexcept {
        const double target = static_cast<double>(targetFps);
        switch (priority) {
            case AdaptiveFractionalRealFramePriority::Auto:
                return 0.0;
            case AdaptiveFractionalRealFramePriority::Low:
                return target * 3.0 / 5.0;
            case AdaptiveFractionalRealFramePriority::Medium:
                return target * 2.0 / 3.0;
            case AdaptiveFractionalRealFramePriority::High:
                return target * 3.0 / 4.0;
            case AdaptiveFractionalRealFramePriority::VeryHigh:
                return target * 4.0 / 5.0;
        }
        return 0.0;
    }

    [[nodiscard]] constexpr const char* scalingMethodName(
            const ScalingMethod method) noexcept {
        switch (method) {
            case ScalingMethod::Native:
                return "native";
            case ScalingMethod::Mako:
                return "mako";
            case ScalingMethod::Ls1:
                return "ls1";
            case ScalingMethod::Ls1Performance:
                return "ls1-performance";
        }
        return "mako";
    }

    [[nodiscard]] constexpr std::optional<ScalingMethod>
    scalingMethodFromName(const std::string_view value) noexcept {
        if (value == "native")
            return ScalingMethod::Native;
        if (value == "mako")
            return ScalingMethod::Mako;
        if (value == "ls1")
            return ScalingMethod::Ls1;
        if (value == "ls1-performance")
            return ScalingMethod::Ls1Performance;
        return std::nullopt;
    }

    /// Renderer defaults used when a game profile omits fields
    struct GameConfDefaults {
        static constexpr FrameGenerationModel frameGenerationModel = FrameGenerationModel::MakoFg;
        static constexpr MakoFgMode makoFgMode = MakoFgMode::Balanced;
        static constexpr size_t multiplier = 2;
        static constexpr bool frameGenerationProvisioned = true;
        static constexpr bool frameGenerationEnabled = true;
        static constexpr bool scalingEnabled = false;
        static constexpr bool swapchainImageCountCompatibility = false;
        static constexpr bool mailboxPresentationCompatibility = false;
        static constexpr ScalingMethod scalingMethod = ScalingMethod::Mako;
        static constexpr float scalingFactor = 1.5F;
        static constexpr bool scalingSupersampling = false;
        static constexpr float scalingSharpness = 0.8F;
        static constexpr HdrQuality hdrQuality = HdrQuality::Performance;
        static constexpr uint32_t frameGenerationRefreshThreshold = 0;
        static constexpr uint32_t baseFpsCap = 0;
        static constexpr uint32_t gpuHeadroomPercent = 0;
        static constexpr bool adaptive = false;
        static constexpr bool adaptiveAutoBaseFpsCap = false;
        static constexpr AdaptiveFractionalRealFramePriority
            adaptiveFractionalRealFramePriority =
                AdaptiveFractionalRealFramePriority::Auto;
        static constexpr uint32_t targetFps = 120;
        static constexpr bool adaptiveTargetRefreshRate = false;
        static constexpr size_t adaptiveMaxMultiplier = 3;
        static constexpr bool adaptiveStableCadence = true;
        static constexpr GamescopeVrrMode gamescopeVrrMode =
            GamescopeVrrMode::FollowSteam;
        static constexpr bool dynamicCadenceRecovery = false;
        static constexpr float dynamicCadenceProbeIntervalSeconds = 2.0F;
        static constexpr bool ultraPerformance = false;
        static constexpr float ultraPerformanceFlowScale = 0.7F;
        static constexpr float ultraPerformanceMakoFgFlowScale = 0.6F;
        static constexpr float flowScale = 0.8F;
        static constexpr bool performanceMode = false;
        static constexpr Pacing pacing = Pacing::None;
    };

    /// ranges accepted by the Renderer configuration parser
    struct GameConfLimits {
        static constexpr size_t minimumMultiplier = 2;
        static constexpr size_t maximumMultiplier = 5;
        static constexpr float minimumScalingFactor = 1.0F;
        static constexpr float maximumScalingFactor = 2.0F;
        static constexpr float minimumScalingSharpness = 0.0F;
        static constexpr float maximumScalingSharpness = 1.0F;
        static constexpr std::array<uint32_t, 5> gpuHeadroomPercentPresets{0, 5, 10, 15, 20};
        static constexpr uint32_t minimumBaseFpsCap = 0;
        static constexpr uint32_t maximumBaseFpsCap = 1000;
        static constexpr uint32_t minimumFrameGenerationRefreshThreshold = 0;
        static constexpr uint32_t maximumFrameGenerationRefreshThreshold = 1000;
        static constexpr uint32_t minimumTargetFps = 10;
        static constexpr uint32_t maximumTargetFps = 1000;
        static constexpr size_t minimumAdaptiveMaxMultiplier = 2;
        static constexpr size_t maximumAdaptiveMaxMultiplier = 5;
        static constexpr float minimumDynamicCadenceProbeIntervalSeconds = 0.1F;
        static constexpr float maximumDynamicCadenceProbeIntervalSeconds = 3.0F;
        static constexpr std::array<float, 9>
            dynamicCadenceProbeIntervalPresetsSeconds{
                0.1F, 0.2F, 0.25F, 0.5F, 0.75F,
                1.0F, 1.5F, 2.0F, 3.0F,
            };
        static constexpr float minimumFlowScale = 0.25F;
        static constexpr float maximumFlowScale = 1.0F;
    };

    enum class PowerSource : uint8_t { Unknown, Handheld, Docked };

    /// Read system power supplies; a dock/display connection alone is irrelevant.
    [[nodiscard]] PowerSource detectPowerSource(
        const std::filesystem::path& root = "/sys/class/power_supply");

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
        /// provision LSFG device interop and backend resources at process start
        bool frame_generation_provisioned{
            GameConfDefaults::frameGenerationProvisioned
        };
        /// allow frame synthesis to be toggled live without changing its mode
        bool frame_generation_enabled{GameConfDefaults::frameGenerationEnabled};
        FrameGenerationModel frame_generation_model{GameConfDefaults::frameGenerationModel};
        /// how often MAKO FG solves motion; Lossless Scaling ignores it
        MakoFgMode mako_fg_mode{GameConfDefaults::makoFgMode};
        /// provision the scaling engine independently from frame synthesis
        bool scaling_enabled{GameConfDefaults::scalingEnabled};
        /// preserve the application's requested swapchain image minimum
        bool swapchain_image_count_compatibility{
            GameConfDefaults::swapchainImageCountCompatibility
        };
        /// present ordered Frame Generation through MAILBOX instead of FIFO
        bool mailbox_presentation_compatibility{
            GameConfDefaults::mailboxPresentationCompatibility
        };
        /// spatial reconstruction implementation
        ScalingMethod scaling_method{GameConfDefaults::scalingMethod};
        /// ratio between the source and output dimensions when scaling
        float scaling_factor{GameConfDefaults::scalingFactor};
        /// allow the scaled presentation to exceed a proven display target
        bool scaling_supersampling{GameConfDefaults::scalingSupersampling};
        /// normalized scaler sharpening strength
        float scaling_sharpness{GameConfDefaults::scalingSharpness};
        /// Select full precision, compact buffers, or approximate HDR10 generation.
        HdrQuality hdr_quality{GameConfDefaults::hdrQuality};
        /// pause synthesis at or below a confirmed refresh rate; zero disables it
        uint32_t frame_generation_refresh_threshold{
            GameConfDefaults::frameGenerationRefreshThreshold
        };
        /// maximum application-present rate before frame generation; zero disables it
        uint32_t base_fps_cap{GameConfDefaults::baseFpsCap};
        /// soft GPU utilization reserve: 0 (off), 5, 10, 15 or 20 percent
        uint32_t gpu_headroom_percent{GameConfDefaults::gpuHeadroomPercent};
        /// dynamically vary the generated-frame count toward a target framerate
        bool adaptive{GameConfDefaults::adaptive};
        /// cap Adaptive's real-frame input to half its target for even 2x cadence
        bool adaptive_auto_base_fps_cap{GameConfDefaults::adaptiveAutoBaseFpsCap};
        /// optional cadence-friendly real-frame share for Fractional Adaptive
        AdaptiveFractionalRealFramePriority
            adaptive_fractional_real_frame_priority{
                GameConfDefaults::adaptiveFractionalRealFramePriority
            };
        /// desired displayed framerate when adaptive mode is enabled
        uint32_t target_fps{GameConfDefaults::targetFps};
        /// Follow confirmed display refresh in Adaptive; target_fps stays the fallback.
        bool adaptive_target_refresh_rate{GameConfDefaults::adaptiveTargetRefreshRate};
        /// maximum total multiplier Adaptive may use
        size_t adaptive_max_multiplier{GameConfDefaults::adaptiveMaxMultiplier};
        /// prefer a validated even display-divisor cadence when safe
        bool adaptive_stable_cadence{GameConfDefaults::adaptiveStableCadence};
        /// opt-in physical VRR override while this profile's game runs
        GamescopeVrrMode gamescope_vrr_mode{GameConfDefaults::gamescopeVrrMode};
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
        /// Optional handheld/docked settings, in that order. Identity stays shared.
        std::vector<GameConf> power_profiles;
    };

    [[nodiscard]] GameConf profileForPowerSource(
        const GameConf& profile, PowerSource source);

    /// Whether this profile should retain the spatial reconstruction lane.
    /// Native Resolution is the model-free baseline inside that lane so a
    /// running process can switch models without replacing its WSI objects.
    [[nodiscard]] constexpr bool spatialScalingRequested(
            const GameConf& profile) noexcept {
        return profile.scaling_enabled;
    }

    /// Whether this profile can change the source or presentation extent.
    /// A 1.0 factor retains the live reconstruction lane but remains spatially
    /// inactive, so it must not alter unrelated swapchain provisioning.
    [[nodiscard]] constexpr bool spatialScalingMayActivate(
            const GameConf& profile) noexcept {
        return spatialScalingRequested(profile) &&
            profile.scaling_factor > GameConfLimits::minimumScalingFactor;
    }

    /// Spatial model selected after applying cross-feature presets. Ultra
    /// Performance keeps a disabled scaling engine dormant, but uses MAKO
    /// Scaler whenever that engine is provisioned, with either FG model.
    [[nodiscard]] constexpr ScalingMethod effectiveScalingMethod(
            const GameConf& profile) noexcept {
        return profile.scaling_enabled && profile.ultra_performance
            ? ScalingMethod::Mako
            : profile.scaling_method;
    }

    /// Whether this method requires user-supplied licensed model resources.
    [[nodiscard]] constexpr bool licensedScalingModelRequested(
            const ScalingMethod method) noexcept {
        return method == ScalingMethod::Ls1 ||
            method == ScalingMethod::Ls1Performance;
    }

    /// Flow scale selected for backend construction after applying presets.
    /// Ultra Performance locks each FG model at its own Flow Scale.
    [[nodiscard]] constexpr float effectiveFlowScale(
            const GameConf& profile) noexcept {
        if (!profile.ultra_performance) return profile.flow_scale;
        return profile.frame_generation_model == FrameGenerationModel::MakoFg
            ? GameConfDefaults::ultraPerformanceMakoFgFlowScale
            : GameConfDefaults::ultraPerformanceFlowScale;
    }

    /// MAKO FG mode for backend construction after applying presets. Ultra
    /// Performance raises it to at least Performance; other models use neither.
    [[nodiscard]] constexpr MakoFgMode effectiveMakoFgMode(
            const GameConf& profile) noexcept {
        if (profile.frame_generation_model != FrameGenerationModel::MakoFg)
            return MakoFgMode::Quality;
        return profile.ultra_performance && profile.mako_fg_mode < MakoFgMode::Performance
            ? MakoFgMode::Performance : profile.mako_fg_mode;
    }

    /// Model selected for backend construction after applying presets. MAKO FG
    /// has no lighter variant, so only LSFG uses it.
    [[nodiscard]] constexpr bool effectivePerformanceMode(
            const GameConf& profile) noexcept {
        if (profile.frame_generation_model == FrameGenerationModel::MakoFg)
            return false;
        return profile.ultra_performance || profile.performance_mode;
    }

    /// The global precision choice takes precedence over every profile preset.
    [[nodiscard]] constexpr bool effectiveAllowFp16(
            const GlobalConf& global, const GameConf& profile) noexcept {
        static_cast<void>(profile);
        return global.allow_fp16;
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

        /// Editor selection used by the native Remote Play launcher only.
        std::optional<std::string> current_profile;

        PowerSource power_source{PowerSource::Unknown};

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
        explicit WatchedConfig(std::filesystem::path powerSupplyRoot = "/sys/class/power_supply");
        ~WatchedConfig();

        WatchedConfig(const WatchedConfig&) = delete;
        WatchedConfig& operator=(const WatchedConfig&) = delete;
        WatchedConfig(WatchedConfig&&) noexcept;
        WatchedConfig& operator=(WatchedConfig&&) noexcept;

        /// reload the configuration from disk if it has changed
        /// @throws ls::error on failure
        /// @return true if the configuration was reloaded
        bool update();

        /// access the underlying configuration file
        /// @return configuration file
        [[nodiscard]] const auto& get() const { return this->configFile; }
    private:
        void configurePowerMonitoring(bool startup);
        bool updatePowerSource();

        ConfigFile configFile;
        std::filesystem::path powerSupplyRoot;
        std::chrono::steady_clock::time_point nextPowerPoll;
        bool powerProfilesEnabled{false};
        std::unique_ptr<detail::PowerSourceMonitor> powerMonitor;

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
