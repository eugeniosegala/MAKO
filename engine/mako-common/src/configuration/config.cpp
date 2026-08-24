/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/configuration/config.hpp"
#include "mako-common/helpers/errors.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <toml.hpp>

using namespace ls;

void ConfigFile::createDefaultConfigFile(const std::filesystem::path& path) {
    try {
        std::filesystem::create_directories(path.parent_path());
        if (!std::filesystem::exists(path.parent_path()))
            throw ls::error("unable to create configuration directory");

        std::ofstream ofs(path);
        if (!ofs.is_open())
            throw ls::error("unable to create default configuration file");

        ofs << "version = " << ConfigFile::formatVersion << R"(

[global]
# dll = '/media/games/Lossless Scaling/Lossless.dll' # if you don't have LS in the default location
allow_fp16 = true # this will improve give a MASSIVE performance boost on AMD, but be super slow on older (!) NVIDIA GPUs

[[profile]]
name = "4x FG / 85% [Performance]"
active_in = [ # see the wiki for more info
    'vkcube',
    'vkcubepp'
]
# gpu = 'NVIDIA GeForce RTX 5080' # see the wiki for more info
multiplier = 4
frame_generation_enabled = true
frame_generation_refresh_threshold = 0
base_fps_cap = 0
adaptive = false
adaptive_auto_base_fps_cap = false
target_fps = 120
adaptive_max_multiplier = 3
adaptive_stable_cadence = false
dynamic_cadence_recovery = false
dynamic_cadence_probe_interval_seconds = 5
flow_scale = 0.85
performance_mode = true
pacing = 'none' # see the wiki for more info

[[profile]]
name = "2x FG / 100%"
active_in = 'GenshinImpact.exe'
gpu = 'NVIDIA GeForce RTX 5080'
multiplier = 2
)";
        ofs.close();
    } catch (const std::filesystem::filesystem_error& e) {
        throw ls::error("unable to create default configuration file", e);
    }
}

ConfigFile::ConfigFile() {
    this->globalConf = {
        .allow_fp16 = true
    };
    this->profileConfs.emplace_back(GameConf {
        .name = "4x FG / 85% [Performance]",
        .active_in = {
            "vkcube",
            "vkcubepp"
        },
        .multiplier = 4,
        .frame_generation_enabled = GameConfDefaults::frameGenerationEnabled,
        .frame_generation_refresh_threshold =
            GameConfDefaults::frameGenerationRefreshThreshold,
        .base_fps_cap = GameConfDefaults::baseFpsCap,
        .adaptive = GameConfDefaults::adaptive,
        .adaptive_auto_base_fps_cap =
            GameConfDefaults::adaptiveAutoBaseFpsCap,
        .target_fps = GameConfDefaults::targetFps,
        .adaptive_max_multiplier = GameConfDefaults::adaptiveMaxMultiplier,
        .adaptive_stable_cadence = GameConfDefaults::adaptiveStableCadence,
        .dynamic_cadence_recovery = GameConfDefaults::dynamicCadenceRecovery,
        .dynamic_cadence_probe_interval_seconds =
            GameConfDefaults::dynamicCadenceProbeIntervalSeconds,
        .flow_scale = 0.85F,
        .performance_mode = true,
        .pacing = GameConfDefaults::pacing
    });
    this->profileConfs.emplace_back(GameConf {
        .name = "2x FG / 100%",
        .active_in = {
            "GenshinImpact.exe"
        },
        .gpu = "NVIDIA GeForce RTX 5080",
        .multiplier = GameConfDefaults::multiplier
    });
}

namespace {
    constexpr auto configurationParseRetryDelay = std::chrono::milliseconds(50);

    std::string formatFloatingLimit(const float value) {
        auto result = std::to_string(value);
        while (result.ends_with('0'))
            result.pop_back();
        if (result.ends_with('.'))
            result.push_back('0');
        return result;
    }

    /// parse an activity array from toml value
    std::vector<std::string> activityFromString(const toml::node_view<const toml::node>& val) {
        std::vector<std::string> active_in{};

        if (const auto& as_str = val.value<std::string>()) {
            active_in.push_back(*as_str);
        }

        if (const auto& as_arr = val.as_array()) {
            for (const auto& item : *as_arr) {
                if (const auto& item_str = item.value<std::string>())
                    active_in.push_back(*item_str);
            }
        }

        return active_in;
    }
    /// parse a pacing method from string
    Pacing parcingFromString(const std::string& str) {
        if (str == "none")
            return Pacing::None;
        throw ls::error("unknown pacing method: " + str);
    }
    /// validate the shared file/environment game-profile contract
    void validateGameConf(const GameConf& conf) {
        if (conf.multiplier < GameConfLimits::minimumMultiplier)
            throw ls::error(
                "multiplier must be greater than " + std::to_string(
                    GameConfLimits::minimumMultiplier - 1
                )
            );
        if (conf.base_fps_cap > GameConfLimits::maximumBaseFpsCap)
            throw ls::error(
                "base_fps_cap must be " + std::to_string(
                    GameConfLimits::minimumBaseFpsCap
                ) + " or between " + std::to_string(
                    GameConfLimits::minimumBaseFpsCap + 1
                ) + " and " + std::to_string(
                    GameConfLimits::maximumBaseFpsCap
                )
            );
        if (conf.frame_generation_refresh_threshold >
                GameConfLimits::maximumFrameGenerationRefreshThreshold) {
            throw ls::error(
                "frame_generation_refresh_threshold must be " + std::to_string(
                    GameConfLimits::minimumFrameGenerationRefreshThreshold
                ) + " or between " + std::to_string(
                    GameConfLimits::minimumFrameGenerationRefreshThreshold + 1
                ) + " and " + std::to_string(
                    GameConfLimits::maximumFrameGenerationRefreshThreshold
                )
            );
        }
        if (conf.target_fps < GameConfLimits::minimumTargetFps ||
                conf.target_fps > GameConfLimits::maximumTargetFps) {
            throw ls::error(
                "target_fps must be between " + std::to_string(
                    GameConfLimits::minimumTargetFps
                ) + " and " + std::to_string(
                    GameConfLimits::maximumTargetFps
                )
            );
        }
        if (conf.adaptive_max_multiplier <
                GameConfLimits::minimumAdaptiveMaxMultiplier ||
                conf.adaptive_max_multiplier >
                    GameConfLimits::maximumAdaptiveMaxMultiplier) {
            throw ls::error(
                "adaptive_max_multiplier must be between " + std::to_string(
                    GameConfLimits::minimumAdaptiveMaxMultiplier
                ) + " and " + std::to_string(
                    GameConfLimits::maximumAdaptiveMaxMultiplier
                )
            );
        }
        if (conf.dynamic_cadence_probe_interval_seconds <
                GameConfLimits::minimumDynamicCadenceProbeIntervalSeconds ||
                conf.dynamic_cadence_probe_interval_seconds >
                    GameConfLimits::maximumDynamicCadenceProbeIntervalSeconds) {
            throw ls::error(
                "dynamic_cadence_probe_interval_seconds must be between " +
                std::to_string(
                    GameConfLimits::minimumDynamicCadenceProbeIntervalSeconds
                ) + " and " + std::to_string(
                    GameConfLimits::maximumDynamicCadenceProbeIntervalSeconds
                )
            );
        }
        if (conf.flow_scale < GameConfLimits::minimumFlowScale ||
                conf.flow_scale > GameConfLimits::maximumFlowScale) {
            throw ls::error(
                "flow_scale must be between " + formatFloatingLimit(
                    GameConfLimits::minimumFlowScale
                ) + " and " + formatFloatingLimit(
                    GameConfLimits::maximumFlowScale
                )
            );
        }
    }
    /// parse the global configuration
    GlobalConf parseGlobalConf(const toml::table& tbl) {
        const GlobalConf conf{
            .dll = tbl["dll"].value<std::string>(),
            .allow_fp16 = tbl["allow_fp16"].value_or(true)
        };

        if (conf.dll && !std::filesystem::exists(*conf.dll))
            throw ls::error("path to dll is invalid");

        return conf;
    }
    /// parse a game profile configuration
    GameConf parseGameConf(const toml::table& tbl) {
        GameConf conf{
            .name = tbl["name"].value_or<std::string>("unnamed"),
            .active_in = activityFromString(tbl["active_in"]),
            .gpu = tbl["gpu"].value<std::string>(),
            .multiplier = tbl["multiplier"].value_or(GameConfDefaults::multiplier),
            .frame_generation_enabled = tbl["frame_generation_enabled"].value_or(
                GameConfDefaults::frameGenerationEnabled
            ),
            .frame_generation_refresh_threshold =
                tbl["frame_generation_refresh_threshold"].value_or(
                    GameConfDefaults::frameGenerationRefreshThreshold
                ),
            .base_fps_cap = tbl["base_fps_cap"].value_or(GameConfDefaults::baseFpsCap),
            .adaptive = tbl["adaptive"].value_or(GameConfDefaults::adaptive),
            .adaptive_auto_base_fps_cap =
                tbl["adaptive_auto_base_fps_cap"].value_or(
                    GameConfDefaults::adaptiveAutoBaseFpsCap
                ),
            .target_fps = tbl["target_fps"].value_or(GameConfDefaults::targetFps),
            .adaptive_max_multiplier = tbl["adaptive_max_multiplier"].value_or(
                GameConfDefaults::adaptiveMaxMultiplier
            ),
            .adaptive_stable_cadence = tbl["adaptive_stable_cadence"].value_or(
                GameConfDefaults::adaptiveStableCadence
            ),
            .dynamic_cadence_recovery = tbl["dynamic_cadence_recovery"].value_or(
                GameConfDefaults::dynamicCadenceRecovery
            ),
            .dynamic_cadence_probe_interval_seconds =
                tbl["dynamic_cadence_probe_interval_seconds"].value_or(
                    GameConfDefaults::dynamicCadenceProbeIntervalSeconds
                ),
            .flow_scale = tbl["flow_scale"].value_or(GameConfDefaults::flowScale),
            .performance_mode = tbl["performance_mode"].value_or(
                GameConfDefaults::performanceMode
            ),
            .pacing = parcingFromString(tbl["pacing"].value_or<std::string>("none"))
        };

        validateGameConf(conf);
        if (conf.dynamic_cadence_recovery) {
            conf.adaptive_auto_base_fps_cap = false;
            conf.base_fps_cap = 0;
        }
        return conf;
    }
    /// parse the global configuration from the environment
    GlobalConf parseGlobalConfFromEnv() {
        GlobalConf conf{
            .dll = std::nullopt,
            .allow_fp16 = true
        };

        const char* dll = std::getenv("MAKO_DLL_PATH");
        if (dll && *dll != '\0')
            conf.dll = std::string(dll);
        const char* no_fp16 = std::getenv("MAKO_NO_FP16");
        if (no_fp16 && *no_fp16 != '\0')
            conf.allow_fp16 = std::string(no_fp16) != "1";

        if (conf.dll && !std::filesystem::exists(*conf.dll))
            throw ls::error("path to dll is invalid");

        return conf;
    }
    /// parse a game profile configuration from the environment
    GameConf parseGameConfFromEnv() {
        GameConf conf{
            .name = "(environment)",
            .active_in = {},
            .gpu = std::nullopt,

            .multiplier = GameConfDefaults::multiplier,
            .frame_generation_enabled = GameConfDefaults::frameGenerationEnabled,
            .frame_generation_refresh_threshold =
                GameConfDefaults::frameGenerationRefreshThreshold,
            .base_fps_cap = GameConfDefaults::baseFpsCap,
            .adaptive = GameConfDefaults::adaptive,
            .adaptive_auto_base_fps_cap =
                GameConfDefaults::adaptiveAutoBaseFpsCap,
            .target_fps = GameConfDefaults::targetFps,
            .adaptive_max_multiplier = GameConfDefaults::adaptiveMaxMultiplier,
            .adaptive_stable_cadence = GameConfDefaults::adaptiveStableCadence,
            .dynamic_cadence_recovery = GameConfDefaults::dynamicCadenceRecovery,
            .dynamic_cadence_probe_interval_seconds =
                GameConfDefaults::dynamicCadenceProbeIntervalSeconds,
            .flow_scale = GameConfDefaults::flowScale,
            .performance_mode = GameConfDefaults::performanceMode,
            .pacing = GameConfDefaults::pacing
        };

        const char* gpu = std::getenv("MAKO_GPU");
        if (gpu) conf.gpu = std::string(gpu);
        const char* multiplier = std::getenv("MAKO_MULTIPLIER");
        if (multiplier) conf.multiplier = static_cast<size_t>(std::stoul(multiplier));
        const char* frame_generation_enabled = std::getenv("MAKO_FRAME_GENERATION_ENABLED");
        if (frame_generation_enabled)
            conf.frame_generation_enabled = std::string(frame_generation_enabled) != "0";
        const char* frame_generation_refresh_threshold =
            std::getenv("MAKO_FRAME_GENERATION_REFRESH_THRESHOLD");
        if (frame_generation_refresh_threshold) {
            conf.frame_generation_refresh_threshold = static_cast<uint32_t>(
                std::stoul(frame_generation_refresh_threshold)
            );
        }
        const char* base_fps_cap = std::getenv("MAKO_BASE_FPS_CAP");
        if (base_fps_cap)
            conf.base_fps_cap = static_cast<uint32_t>(std::stoul(base_fps_cap));
        const char* adaptive = std::getenv("MAKO_ADAPTIVE");
        if (adaptive) conf.adaptive = std::string(adaptive) == "1";
        const char* adaptive_auto_base_fps_cap =
            std::getenv("MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP");
        if (adaptive_auto_base_fps_cap) {
            conf.adaptive_auto_base_fps_cap =
                std::string(adaptive_auto_base_fps_cap) != "0";
        }
        const char* target_fps = std::getenv("MAKO_TARGET_FPS");
        if (target_fps) conf.target_fps = static_cast<uint32_t>(std::stoul(target_fps));
        const char* adaptive_max_multiplier = std::getenv("MAKO_ADAPTIVE_MAX_MULTIPLIER");
        if (adaptive_max_multiplier)
            conf.adaptive_max_multiplier = static_cast<size_t>(std::stoul(adaptive_max_multiplier));
        const char* adaptive_stable_cadence = std::getenv("MAKO_ADAPTIVE_STABLE_CADENCE");
        if (adaptive_stable_cadence)
            conf.adaptive_stable_cadence = std::string(adaptive_stable_cadence) != "0";
        const char* dynamic_cadence_recovery =
            std::getenv("MAKO_DYNAMIC_CADENCE_RECOVERY");
        if (dynamic_cadence_recovery) {
            conf.dynamic_cadence_recovery =
                std::string(dynamic_cadence_recovery) != "0";
        }
        const char* dynamic_cadence_probe_interval_seconds =
            std::getenv("MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS");
        if (dynamic_cadence_probe_interval_seconds) {
            conf.dynamic_cadence_probe_interval_seconds =
                static_cast<uint32_t>(std::stoul(
                    dynamic_cadence_probe_interval_seconds
                ));
        }
        const char* flow_scale = std::getenv("MAKO_FLOW_SCALE");
        if (flow_scale) conf.flow_scale = std::stof(flow_scale);
        const char* performance = std::getenv("MAKO_PERFORMANCE_MODE");
        if (performance) conf.performance_mode = std::string(performance) == "1";
        const char* pacing = std::getenv("MAKO_PACING");
        if (pacing) conf.pacing = parcingFromString(std::string(pacing));

        validateGameConf(conf);
        if (conf.dynamic_cadence_recovery) {
            conf.adaptive_auto_base_fps_cap = false;
            conf.base_fps_cap = 0;
        }
        return conf;
    }
}

ConfigFile::ConfigFile(const std::filesystem::path& path) {
    toml::table table;
    try {
        table = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        throw ls::error("unable to parse configuration", e);
    }

    auto version = table["version"];
    if (!version || !version.is_integer() ||
            *version.as_integer() != ConfigFile::formatVersion) {
        throw ls::error("unsupported configuration version");
    }

    auto global = table["global"];
    if (global && global.is_table()) {
        this->globalConf = parseGlobalConf(*global.as_table());
    }

    auto profiles = table["profile"];
    if (profiles && profiles.is_array_of_tables())
        for (const auto& profile : *profiles.as_array())
            this->profileConfs.push_back(parseGameConf(*profile.as_table()));
}

void ConfigFile::write(const std::filesystem::path& path) const {
    toml::table table;
    table.insert("version", ConfigFile::formatVersion);

    toml::table global;
    if (this->globalConf.dll)
        global.insert("dll", *this->globalConf.dll);
    global.insert("allow_fp16", this->globalConf.allow_fp16);
    table.insert("global", global);

    toml::array profiles;
    for (const auto& conf : this->profileConfs) {
        toml::table profile;
        profile.insert("name", conf.name);

        if (!conf.active_in.empty()) {
            if (conf.active_in.size() == 1) {
                profile.insert("active_in", conf.active_in.front());
            } else {
                toml::array active_in;
                for (const auto& entry : conf.active_in)
                    active_in.push_back(entry);
                profile.insert("active_in", active_in);
            }
        }
        if (conf.gpu)
            profile.insert("gpu", conf.gpu.value_or(""));
        profile.insert("multiplier", static_cast<int64_t>(conf.multiplier));
        profile.insert("frame_generation_enabled", conf.frame_generation_enabled);
        profile.insert(
            "frame_generation_refresh_threshold",
            static_cast<int64_t>(conf.frame_generation_refresh_threshold)
        );
        profile.insert("base_fps_cap", static_cast<int64_t>(conf.base_fps_cap));
        profile.insert("adaptive", conf.adaptive);
        profile.insert(
            "adaptive_auto_base_fps_cap", conf.adaptive_auto_base_fps_cap
        );
        profile.insert("target_fps", static_cast<int64_t>(conf.target_fps));
        profile.insert("adaptive_max_multiplier", static_cast<int64_t>(conf.adaptive_max_multiplier));
        profile.insert("adaptive_stable_cadence", conf.adaptive_stable_cadence);
        profile.insert("dynamic_cadence_recovery", conf.dynamic_cadence_recovery);
        profile.insert(
            "dynamic_cadence_probe_interval_seconds",
            static_cast<int64_t>(conf.dynamic_cadence_probe_interval_seconds)
        );
        profile.insert("flow_scale", conf.flow_scale);
        profile.insert("performance_mode", conf.performance_mode);
        switch (conf.pacing) {
            case Pacing::None:
                profile.insert("pacing", "none");
                break;
        }

        profiles.push_back(profile);
    }
    table.insert("profile", profiles);

    try {
        std::ofstream ofs(path);
        if (!ofs.is_open())
            throw ls::error("unable to open configuration file for writing");

        ofs << toml::toml_formatter {
            table,
            toml::v3::format_flags::relaxed_float_precision
        } << '\n';
        ofs.close();
    } catch (const std::exception& e) {
        throw ls::error("unable to write configuration file", e);
    }
}

WatchedConfig::WatchedConfig() : path(findConfigurationFile()) {
    if (std::getenv("MAKO_ENV")) {
        auto& config = this->configFile;
        config.global() = parseGlobalConfFromEnv();
        config.profiles() = { parseGameConfFromEnv() };

        return;
    }

    if (!std::filesystem::exists(this->path))
        ConfigFile::createDefaultConfigFile(this->path);

    this->configFile = ConfigFile(this->path);
    this->last_timestamp = std::filesystem::last_write_time(this->path);
}

bool WatchedConfig::update() {
    if (std::getenv("MAKO_ENV"))
        return false;

    const auto now = std::filesystem::last_write_time(this->path);
    if (now == this->last_timestamp)
        return false;

    const auto retryNow = std::chrono::steady_clock::now();
    if (this->failed_timestamp && *this->failed_timestamp == now &&
            retryNow < this->next_parse_retry)
        return false;

    // Advance the observed timestamp only after parsing succeeds. Decky may
    // briefly expose a partially-written TOML file; retaining the old stamp
    // makes the same version retryable instead of silently losing the update.
    ConfigFile new_config;
    try {
        new_config = ConfigFile(this->path);
    } catch (...) {
        this->failed_timestamp = now;
        this->next_parse_retry = retryNow + configurationParseRetryDelay;
        throw;
    }
    this->configFile = std::move(new_config);
    this->last_timestamp = now;
    this->failed_timestamp.reset();
    return true;
}

std::filesystem::path ls::findConfigurationFile() {
    // always honor MAKO_CONFIG if set
    const char* envPath = std::getenv("MAKO_CONFIG");
    if (envPath && *envPath != '\0')
        return{envPath};

    // then check the XDG overriden location
    const char* xdgPath = std::getenv("XDG_CONFIG_HOME");
    if (xdgPath && *xdgPath != '\0')
        return std::filesystem::path(xdgPath)
            / "mako-render" / "conf.toml";

    // fallback to typical user home
    const char* homePath = std::getenv("HOME");
    if (homePath && *homePath != '\0')
        return std::filesystem::path(homePath)
            / ".config" / "mako-render" / "conf.toml";

    // finally, use system-wide config
    return "/etc/mako-render/conf.toml";
}
