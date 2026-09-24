/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <QObject>
#include <QStringListModel>
#include <QString>
#include <QVariantList>
#include <QTimer>
#include <QThread>
#include <QJsonObject>

#include "process_detection.hpp"

#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/launch.hpp"
#include "mako-common/configuration/vkbasalt.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <map>
#include <utility>

#define getters public
#define setters public

namespace mako::ui {

    /// Class tying ui and configuration together
    class Backend : public QObject {
        Q_OBJECT

        Q_PROPERTY(QStringListModel* profiles READ calculateProfileListModel NOTIFY refreshUI)
        Q_PROPERTY(int profile_index READ getProfileIndex WRITE profileSelected NOTIFY refreshUI)
        Q_PROPERTY(QVariantList running_games READ getRunningGames NOTIFY runningGamesChanged)
        Q_PROPERTY(bool scanning_games READ isScanningGames NOTIFY runningGamesChanged)
        Q_PROPERTY(bool capture_failed READ captureFailed NOTIFY runningGamesChanged)

        Q_PROPERTY(QString dll READ getDll WRITE dllUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool allow_fp16 READ getAllowFP16 WRITE allowFP16Updated NOTIFY refreshUI)
        Q_PROPERTY(bool enable_zink READ getEnableZink WRITE enableZinkUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool force_alsa_audio READ getForceAlsaAudio WRITE forceAlsaAudioUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool enable_vkbasalt READ getEnableVkBasalt WRITE enableVkBasaltUpdated NOTIFY refreshUI)
        Q_PROPERTY(QString vkbasalt_sharpening READ getVkBasaltSharpening WRITE vkBasaltSharpeningUpdated NOTIFY refreshUI)
        Q_PROPERTY(float vkbasalt_sharpness READ getVkBasaltSharpness WRITE vkBasaltSharpnessUpdated NOTIFY refreshUI)
        Q_PROPERTY(float vkbasalt_dls_denoise READ getVkBasaltDlsDenoise WRITE vkBasaltDlsDenoiseUpdated NOTIFY refreshUI)
        Q_PROPERTY(QString vkbasalt_antialiasing READ getVkBasaltAntialiasing WRITE vkBasaltAntialiasingUpdated NOTIFY refreshUI)
        Q_PROPERTY(QString vkbasalt_shader READ getVkBasaltShader WRITE vkBasaltShaderUpdated NOTIFY refreshUI)
        Q_PROPERTY(QString vkbasalt_config_path READ getVkBasaltConfigPath NOTIFY refreshUI)
        Q_PROPERTY(QString launch_option READ getLaunchOption NOTIFY refreshUI)

        Q_PROPERTY(bool available READ isValidProfileIndex NOTIFY refreshUI)
        Q_PROPERTY(QStringListModel* active_in READ calculateActiveInModel NOTIFY refreshUI)
        Q_PROPERTY(int active_in_index READ getActiveInIndex WRITE activeInSelected NOTIFY refreshUI)
        Q_PROPERTY(QString matched_processes READ getMatchedProcesses NOTIFY refreshUI)
        Q_PROPERTY(size_t multiplier READ getMultiplier WRITE multiplierUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool frame_generation_provisioned READ getFrameGenerationProvisioned WRITE frameGenerationProvisionedUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool frame_generation_enabled READ getFrameGenerationEnabled WRITE frameGenerationEnabledUpdated NOTIFY refreshUI)
        Q_PROPERTY(uint frame_generation_factor_index READ getFrameGenerationFactorIndex WRITE frameGenerationFactorIndexUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool scaling_enabled READ getScalingEnabled WRITE scalingEnabledUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool swapchain_image_count_compatibility READ getSwapchainImageCountCompatibility WRITE swapchainImageCountCompatibilityUpdated NOTIFY refreshUI)
        Q_PROPERTY(QString scaling_method READ getScalingMethod WRITE scalingMethodUpdated NOTIFY refreshUI)
        Q_PROPERTY(float scaling_factor READ getScalingFactor WRITE scalingFactorUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool scaling_supersampling READ getScalingSupersampling WRITE scalingSupersamplingUpdated NOTIFY refreshUI)
        Q_PROPERTY(float scaling_sharpness READ getScalingSharpness WRITE scalingSharpnessUpdated NOTIFY refreshUI)
        Q_PROPERTY(uint frame_generation_refresh_threshold READ getFrameGenerationRefreshThreshold WRITE frameGenerationRefreshThresholdUpdated NOTIFY refreshUI)
        Q_PROPERTY(uint base_fps_cap READ getBaseFPSCap WRITE baseFPSCapUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool adaptive READ getAdaptive WRITE adaptiveUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool fractional_adaptive READ getFractionalAdaptive WRITE fractionalAdaptiveUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool adaptive_auto_base_fps_cap READ getAdaptiveAutoBaseFPSCap WRITE adaptiveAutoBaseFPSCapUpdated NOTIFY refreshUI)
        Q_PROPERTY(QString adaptive_fractional_real_frame_priority READ getAdaptiveFractionalRealFramePriority WRITE adaptiveFractionalRealFramePriorityUpdated NOTIFY refreshUI)
        Q_PROPERTY(double adaptive_fractional_real_frame_priority_cap READ getAdaptiveFractionalRealFramePriorityCap NOTIFY refreshUI)
        Q_PROPERTY(uint target_fps READ getTargetFPS WRITE targetFPSUpdated NOTIFY refreshUI)
        Q_PROPERTY(size_t adaptive_max_multiplier READ getAdaptiveMaxMultiplier WRITE adaptiveMaxMultiplierUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool adaptive_stable_cadence READ getAdaptiveStableCadence WRITE adaptiveStableCadenceUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool dynamic_cadence_recovery READ getDynamicCadenceRecovery WRITE dynamicCadenceRecoveryUpdated NOTIFY refreshUI)
        Q_PROPERTY(double dynamic_cadence_probe_interval_seconds READ getDynamicCadenceProbeIntervalSeconds WRITE dynamicCadenceProbeIntervalSecondsUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool ultra_performance READ getUltraPerformance WRITE ultraPerformanceUpdated NOTIFY refreshUI)
        Q_PROPERTY(float flow_scale READ getFlowScale WRITE flowScaleUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool performance_mode READ getPerformanceMode WRITE performanceModeUpdated NOTIFY refreshUI)
        Q_PROPERTY(QStringList gpus READ calculateGPUList NOTIFY refreshUI)
        Q_PROPERTY(int gpu READ getGPU WRITE gpuUpdated NOTIFY refreshUI)

        Q_PROPERTY(uint minimum_multiplier READ getMinimumMultiplier CONSTANT)
        Q_PROPERTY(uint maximum_multiplier READ getMaximumMultiplier CONSTANT)
        Q_PROPERTY(float minimum_scaling_factor READ getMinimumScalingFactor CONSTANT)
        Q_PROPERTY(float maximum_scaling_factor READ getMaximumScalingFactor CONSTANT)
        Q_PROPERTY(float minimum_scaling_sharpness READ getMinimumScalingSharpness CONSTANT)
        Q_PROPERTY(float maximum_scaling_sharpness READ getMaximumScalingSharpness CONSTANT)
        Q_PROPERTY(uint maximum_frame_generation_refresh_threshold READ getMaximumFrameGenerationRefreshThreshold CONSTANT)
        Q_PROPERTY(uint frame_generation_refresh_threshold_preset READ getFrameGenerationRefreshThresholdPreset CONSTANT)
        Q_PROPERTY(uint minimum_base_fps_cap READ getMinimumBaseFPSCap CONSTANT)
        Q_PROPERTY(uint maximum_base_fps_cap READ getMaximumBaseFPSCap CONSTANT)
        Q_PROPERTY(uint minimum_target_fps READ getMinimumTargetFPS CONSTANT)
        Q_PROPERTY(uint maximum_target_fps READ getMaximumTargetFPS CONSTANT)
        Q_PROPERTY(uint minimum_adaptive_max_multiplier READ getMinimumAdaptiveMaxMultiplier CONSTANT)
        Q_PROPERTY(uint maximum_adaptive_max_multiplier READ getMaximumAdaptiveMaxMultiplier CONSTANT)
        Q_PROPERTY(double minimum_dynamic_cadence_probe_interval_seconds READ getMinimumDynamicCadenceProbeIntervalSeconds CONSTANT)
        Q_PROPERTY(double maximum_dynamic_cadence_probe_interval_seconds READ getMaximumDynamicCadenceProbeIntervalSeconds CONSTANT)
        Q_PROPERTY(QVariantList dynamic_cadence_probe_interval_presets_seconds READ getDynamicCadenceProbeIntervalPresetsSeconds CONSTANT)
        Q_PROPERTY(float minimum_flow_scale READ getMinimumFlowScale CONSTANT)
        Q_PROPERTY(float maximum_flow_scale READ getMaximumFlowScale CONSTANT)
        Q_PROPERTY(float minimum_vkbasalt_strength READ getMinimumVkBasaltStrength CONSTANT)
        Q_PROPERTY(float maximum_vkbasalt_strength READ getMaximumVkBasaltStrength CONSTANT)

    public:
        explicit Backend(std::filesystem::path procRoot = "/proc");
        ~Backend() override;

        [[nodiscard]] QVariantList getRunningGames() const;
        [[nodiscard]] bool isScanningGames() const { return m_scanning_games; }
        [[nodiscard]] bool captureFailed() const { return m_capture_failed; }
        Q_INVOKABLE void refreshRunningGames(bool includeAllApplications = false);
        Q_INVOKABLE bool captureRunningGame(int index, bool createProfile);
        Q_INVOKABLE bool openVkBasaltConfig();

        [[nodiscard]] static bool isFractionalAdaptivePresetEnabled(
                const ls::GameConf& conf) noexcept {
            return conf.adaptive && !conf.adaptive_auto_base_fps_cap;
        }

        static void applyFractionalAdaptivePreset(
                ls::GameConf& conf, bool enabled) noexcept {
            conf.dynamic_cadence_recovery = false;
            conf.adaptive_auto_base_fps_cap = !enabled;
            if (enabled)
                conf.adaptive = true;
        }

        [[nodiscard]] static uint frameGenerationFactorIndex(
                const ls::GameConf& conf) noexcept {
            if (!conf.frame_generation_enabled)
                return 0;
            if (conf.adaptive) {
                return static_cast<uint>(std::clamp(
                    conf.adaptive_max_multiplier,
                    ls::GameConfLimits::minimumAdaptiveMaxMultiplier,
                    ls::GameConfLimits::maximumAdaptiveMaxMultiplier
                ) - ls::GameConfLimits::minimumAdaptiveMaxMultiplier + 1);
            }
            return static_cast<uint>(std::clamp(
                conf.multiplier,
                ls::GameConfLimits::minimumMultiplier,
                ls::GameConfLimits::maximumMultiplier
            ) - ls::GameConfLimits::minimumMultiplier + 1);
        }

        static void applyFrameGenerationFactorIndex(
                ls::GameConf& conf, const uint index) noexcept {
            if (index == 0) {
                conf.frame_generation_enabled = false;
                return;
            }
            conf.frame_generation_enabled = true;
            if (conf.adaptive) {
                conf.adaptive_max_multiplier = std::clamp(
                    ls::GameConfLimits::minimumAdaptiveMaxMultiplier + index - 1,
                    ls::GameConfLimits::minimumAdaptiveMaxMultiplier,
                    ls::GameConfLimits::maximumAdaptiveMaxMultiplier
                );
            } else {
                conf.multiplier = std::clamp(
                    ls::GameConfLimits::minimumMultiplier + index - 1,
                    ls::GameConfLimits::minimumMultiplier,
                    ls::GameConfLimits::maximumMultiplier
                );
            }
        }

    getters:
        [[nodiscard]] QStringListModel* calculateProfileListModel() const {
            return this->m_profile_list_model;
        }
        [[nodiscard]] int getProfileIndex() const {
            return this->m_profile_index;
        }

        [[nodiscard]] QString getDll() const {
            return QString::fromStdString(this->m_global.dll.value_or(""));
        }
        [[nodiscard]] bool getAllowFP16() const {
            return this->m_global.allow_fp16;
        }
        [[nodiscard]] bool getEnableZink() const {
            return this->m_launch.enable_zink;
        }
        [[nodiscard]] bool getForceAlsaAudio() const {
            return this->m_launch.force_alsa_audio;
        }
        [[nodiscard]] bool getEnableVkBasalt() const {
            if (!isValidProfileIndex()) return false;
            return this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).enabled;
        }
        [[nodiscard]] QString getVkBasaltSharpening() const {
            if (!isValidProfileIndex()) return QStringLiteral("cas");
            return QString::fromStdString(this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).sharpening);
        }
        [[nodiscard]] float getVkBasaltSharpness() const {
            if (!isValidProfileIndex()) return 0.5F;
            return this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).sharpness;
        }
        [[nodiscard]] float getVkBasaltDlsDenoise() const {
            if (!isValidProfileIndex()) return 0.2F;
            return this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).dls_denoise;
        }
        [[nodiscard]] QString getVkBasaltAntialiasing() const {
            if (!isValidProfileIndex()) return QStringLiteral("none");
            return QString::fromStdString(this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).antialiasing);
        }
        [[nodiscard]] QString getVkBasaltShader() const {
            if (!isValidProfileIndex()) return QStringLiteral("none");
            return QString::fromStdString(this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).shader);
        }
        [[nodiscard]] QString getVkBasaltConfigPath() const {
            if (!isValidProfileIndex()) return {};
            return QString::fromStdString(vkBasaltConfigPath(
                static_cast<size_t>(this->m_profile_index)
            ).string());
        }
        [[nodiscard]] QString getLaunchOption() const;

#define VALIDATE_AND_GET_PROFILE(default) \
    if (!isValidProfileIndex()) return default; \
    auto& conf = this->m_profiles.at(static_cast<size_t>(this->m_profile_index));

        [[nodiscard]] bool isValidProfileIndex() const {
            return this->m_profile_index >= 0 && std::cmp_less(this->m_profile_index, this->m_profiles.size());
        }
        [[nodiscard]] QStringListModel* calculateActiveInModel() const {
            if (!isValidProfileIndex()) return nullptr;
            return this->m_active_in_list_models.at(static_cast<size_t>(this->m_profile_index));
        }
        [[nodiscard]] int getActiveInIndex() const {
            if (!isValidProfileIndex()) return -1;
            return static_cast<int>(this->m_active_in_index);
        }
        [[nodiscard]] QString getMatchedProcesses() const {
            if (!isValidProfileIndex()) return {};
            QStringList processes;
            for (const auto& process : this->m_profiles.at(
                    static_cast<size_t>(this->m_profile_index)).active_in) {
                processes.append(QString::fromStdString(process));
            }
            return processes.join(", ");
        }

        [[nodiscard]] size_t getMultiplier() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::multiplier)
            return conf.multiplier;
        }
        [[nodiscard]] bool getFrameGenerationProvisioned() const {
            VALIDATE_AND_GET_PROFILE(
                ls::GameConfDefaults::frameGenerationProvisioned
            )
            return conf.frame_generation_provisioned;
        }
        [[nodiscard]] bool getFrameGenerationEnabled() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::frameGenerationEnabled)
            return conf.frame_generation_enabled;
        }
        [[nodiscard]] uint getFrameGenerationFactorIndex() const {
            VALIDATE_AND_GET_PROFILE(1U)
            return frameGenerationFactorIndex(conf);
        }
        [[nodiscard]] bool getScalingEnabled() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::scalingEnabled)
            return conf.scaling_enabled;
        }
        [[nodiscard]] bool getSwapchainImageCountCompatibility() const {
            VALIDATE_AND_GET_PROFILE(
                ls::GameConfDefaults::swapchainImageCountCompatibility
            )
            return conf.swapchain_image_count_compatibility;
        }
        [[nodiscard]] QString getScalingMethod() const {
            VALIDATE_AND_GET_PROFILE(
                QString::fromUtf8(
                    ls::scalingMethodName(ls::GameConfDefaults::scalingMethod)
                )
            )
            return QString::fromUtf8(
                ls::scalingMethodName(ls::effectiveScalingMethod(conf))
            );
        }
        [[nodiscard]] float getScalingFactor() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::scalingFactor)
            return conf.scaling_factor;
        }
        [[nodiscard]] bool getScalingSupersampling() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::scalingSupersampling)
            return conf.scaling_supersampling;
        }
        [[nodiscard]] float getScalingSharpness() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::scalingSharpness)
            return conf.scaling_sharpness;
        }
        [[nodiscard]] uint getFrameGenerationRefreshThreshold() const {
            VALIDATE_AND_GET_PROFILE(
                ls::GameConfDefaults::frameGenerationRefreshThreshold
            )
            return conf.frame_generation_refresh_threshold;
        }
        [[nodiscard]] uint getBaseFPSCap() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::baseFpsCap)
            return conf.base_fps_cap;
        }
        [[nodiscard]] bool getAdaptive() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::adaptive)
            return conf.adaptive;
        }
        [[nodiscard]] bool getFractionalAdaptive() const {
            VALIDATE_AND_GET_PROFILE(false)
            return isFractionalAdaptivePresetEnabled(conf);
        }
        [[nodiscard]] bool getAdaptiveAutoBaseFPSCap() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::adaptiveAutoBaseFpsCap)
            return conf.adaptive_auto_base_fps_cap;
        }
        [[nodiscard]] QString getAdaptiveFractionalRealFramePriority() const {
            VALIDATE_AND_GET_PROFILE(QStringLiteral("auto"))
            return QString::fromUtf8(
                ls::adaptiveFractionalRealFramePriorityName(
                    conf.adaptive_fractional_real_frame_priority
                )
            );
        }
        [[nodiscard]] double getAdaptiveFractionalRealFramePriorityCap() const {
            VALIDATE_AND_GET_PROFILE(0.0)
            if (!isFractionalAdaptivePresetEnabled(conf))
                return 0.0;
            return ls::adaptiveFractionalRealFramePriorityCap(
                conf.adaptive_fractional_real_frame_priority,
                conf.target_fps
            );
        }
        Q_INVOKABLE double fractionalRealFramePriorityCapFor(
                const QString& priority) const {
            VALIDATE_AND_GET_PROFILE(0.0)
            const auto parsed = ls::adaptiveFractionalRealFramePriorityFromName(
                priority.toStdString()
            );
            return parsed ? ls::adaptiveFractionalRealFramePriorityCap(
                *parsed, conf.target_fps
            ) : 0.0;
        }
        [[nodiscard]] uint getTargetFPS() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::targetFps)
            return conf.target_fps;
        }
        [[nodiscard]] size_t getAdaptiveMaxMultiplier() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::adaptiveMaxMultiplier)
            return conf.adaptive_max_multiplier;
        }
        [[nodiscard]] bool getAdaptiveStableCadence() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::adaptiveStableCadence)
            return conf.adaptive_stable_cadence;
        }
        [[nodiscard]] bool getDynamicCadenceRecovery() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::dynamicCadenceRecovery)
            return conf.dynamic_cadence_recovery;
        }
        [[nodiscard]] double getDynamicCadenceProbeIntervalSeconds() const {
            VALIDATE_AND_GET_PROFILE(
                ls::GameConfDefaults::dynamicCadenceProbeIntervalSeconds
            )
            return conf.dynamic_cadence_probe_interval_seconds;
        }
        [[nodiscard]] bool getUltraPerformance() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::ultraPerformance)
            return conf.ultra_performance;
        }
        [[nodiscard]] float getFlowScale() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::flowScale)
            return ls::effectiveFlowScale(conf);
        }
        [[nodiscard]] bool getPerformanceMode() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::performanceMode)
            return ls::effectivePerformanceMode(conf);
        }
        [[nodiscard]] QStringList calculateGPUList() const {
            return this->m_gpu_list;
        }
        [[nodiscard]] int getGPU() const {
            VALIDATE_AND_GET_PROFILE(0)
            auto gpu = QString::fromStdString(conf.gpu.value_or("Default"));
            return static_cast<int>(this->m_gpu_list.indexOf(gpu));
        }

        [[nodiscard]] uint getMinimumMultiplier() const noexcept {
            return static_cast<uint>(ls::GameConfLimits::minimumMultiplier);
        }
        [[nodiscard]] uint getMaximumMultiplier() const noexcept {
            return static_cast<uint>(ls::GameConfLimits::maximumMultiplier);
        }
        [[nodiscard]] float getMinimumScalingFactor() const noexcept {
            return ls::GameConfLimits::minimumScalingFactor;
        }
        [[nodiscard]] float getMaximumScalingFactor() const noexcept {
            return ls::GameConfLimits::maximumScalingFactor;
        }
        [[nodiscard]] float getMinimumScalingSharpness() const noexcept {
            return ls::GameConfLimits::minimumScalingSharpness;
        }
        [[nodiscard]] float getMaximumScalingSharpness() const noexcept {
            return ls::GameConfLimits::maximumScalingSharpness;
        }
        [[nodiscard]] uint getMaximumFrameGenerationRefreshThreshold()
                const noexcept {
            return ls::GameConfLimits::maximumFrameGenerationRefreshThreshold;
        }
        [[nodiscard]] uint getFrameGenerationRefreshThresholdPreset()
                const noexcept {
            return 60;
        }
        [[nodiscard]] uint getMinimumBaseFPSCap() const noexcept {
            return ls::GameConfLimits::minimumBaseFpsCap;
        }
        [[nodiscard]] uint getMaximumBaseFPSCap() const noexcept {
            return ls::GameConfLimits::maximumBaseFpsCap;
        }
        [[nodiscard]] uint getMinimumTargetFPS() const noexcept {
            return ls::GameConfLimits::minimumTargetFps;
        }
        [[nodiscard]] uint getMaximumTargetFPS() const noexcept {
            return ls::GameConfLimits::maximumTargetFps;
        }
        [[nodiscard]] uint getMinimumAdaptiveMaxMultiplier() const noexcept {
            return static_cast<uint>(
                ls::GameConfLimits::minimumAdaptiveMaxMultiplier
            );
        }
        [[nodiscard]] uint getMaximumAdaptiveMaxMultiplier() const noexcept {
            return static_cast<uint>(
                ls::GameConfLimits::maximumAdaptiveMaxMultiplier
            );
        }
        [[nodiscard]] double getMinimumDynamicCadenceProbeIntervalSeconds()
                const noexcept {
            return ls::GameConfLimits::
                minimumDynamicCadenceProbeIntervalSeconds;
        }
        [[nodiscard]] double getMaximumDynamicCadenceProbeIntervalSeconds()
                const noexcept {
            return ls::GameConfLimits::
                maximumDynamicCadenceProbeIntervalSeconds;
        }
        [[nodiscard]] QVariantList
                getDynamicCadenceProbeIntervalPresetsSeconds() const {
            QVariantList presets;
            presets.reserve(static_cast<qsizetype>(ls::GameConfLimits::
                dynamicCadenceProbeIntervalPresetsSeconds.size()));
            for (const float seconds : ls::GameConfLimits::
                    dynamicCadenceProbeIntervalPresetsSeconds) {
                presets.append(static_cast<double>(seconds));
            }
            return presets;
        }
        [[nodiscard]] float getMinimumFlowScale() const noexcept {
            return ls::GameConfLimits::minimumFlowScale;
        }
        [[nodiscard]] float getMaximumFlowScale() const noexcept {
            return ls::GameConfLimits::maximumFlowScale;
        }
        [[nodiscard]] float getMinimumVkBasaltStrength() const noexcept {
            return ls::vkBasaltStrengthMinimum;
        }
        [[nodiscard]] float getMaximumVkBasaltStrength() const noexcept {
            return ls::vkBasaltStrengthMaximum;
        }

#undef VALIDATE_AND_GET_PROFILE

    setters:
        void profileSelected(int idx) {
            this->m_profile_index = idx;
            emit refreshUI();
        }

        void activeInSelected(int idx) {
            this->m_active_in_index = idx;
            emit refreshUI();
        }

#define MARK_DIRTY() \
    this->m_config_dirty = true; \
    this->m_save_timer.start(); \
    emit refreshUI();

#define MARK_LAUNCH_DIRTY() \
    this->m_launch_dirty = true; \
    this->m_save_timer.start(); \
    emit refreshUI();

#define MARK_SHADER_DIRTY() \
    this->m_vkbasalt_dirty = true; \
    this->m_save_timer.start(); \
    emit refreshUI();

        void dllUpdated(const QString& dll) {
            auto& conf = this->m_global;
            if (dll.trimmed().isEmpty())
                conf.dll = std::nullopt;
            else
                conf.dll = dll.toStdString();
            MARK_DIRTY()
        }
        void allowFP16Updated(bool allow_fp16) {
            if (getUltraPerformance()) return;
            auto& conf = this->m_global;
            conf.allow_fp16 = allow_fp16;
            MARK_DIRTY()
        }
        void enableZinkUpdated(bool enable_zink) {
            this->m_launch.enable_zink = enable_zink;
            MARK_LAUNCH_DIRTY()
        }
        void forceAlsaAudioUpdated(bool force_alsa_audio) {
            this->m_launch.force_alsa_audio = force_alsa_audio;
            MARK_LAUNCH_DIRTY()
        }
        void enableVkBasaltUpdated(bool enabled) {
            if (!isValidProfileIndex()) return;
            this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).enabled = enabled;
            MARK_SHADER_DIRTY()
        }
        void vkBasaltSharpeningUpdated(const QString& sharpening) {
            const auto value = sharpening.toStdString();
            if (!ls::isVkBasaltSharpening(value)) return;
            if (!isValidProfileIndex()) return;
            this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).sharpening = value;
            MARK_SHADER_DIRTY()
        }
        void vkBasaltSharpnessUpdated(float sharpness) {
            if (!std::isfinite(sharpness)) return;
            if (!isValidProfileIndex()) return;
            this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).sharpness = std::clamp(
                sharpness,
                ls::vkBasaltStrengthMinimum,
                ls::vkBasaltStrengthMaximum
            );
            MARK_SHADER_DIRTY()
        }
        void vkBasaltDlsDenoiseUpdated(float denoise) {
            if (!std::isfinite(denoise)) return;
            if (!isValidProfileIndex()) return;
            this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).dls_denoise = std::clamp(
                denoise,
                ls::vkBasaltStrengthMinimum,
                ls::vkBasaltStrengthMaximum
            );
            MARK_SHADER_DIRTY()
        }
        void vkBasaltAntialiasingUpdated(const QString& antialiasing) {
            const auto value = antialiasing.toStdString();
            if (!ls::isVkBasaltAntialiasing(value)) return;
            if (!isValidProfileIndex()) return;
            this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).antialiasing = value;
            MARK_SHADER_DIRTY()
        }
        void vkBasaltShaderUpdated(const QString& shader) {
            const auto value = shader.toStdString();
            if (!ls::isVkBasaltShader(value)) return;
            if (!isValidProfileIndex()) return;
            this->m_vkbasalt_profiles.at(
                static_cast<size_t>(this->m_profile_index)
            ).shader = value;
            MARK_SHADER_DIRTY()
        }

#define VALIDATE_AND_GET_PROFILE() \
    if (!isValidProfileIndex()) return; \
    auto& conf = this->m_profiles.at(static_cast<size_t>(this->m_profile_index));

        void multiplierUpdated(size_t multiplier) {
            VALIDATE_AND_GET_PROFILE()
            conf.multiplier = std::clamp(
                multiplier,
                ls::GameConfLimits::minimumMultiplier,
                ls::GameConfLimits::maximumMultiplier
            );
            MARK_DIRTY()
        }
        void frameGenerationProvisionedUpdated(
                bool frame_generation_provisioned) {
            VALIDATE_AND_GET_PROFILE()
            conf.frame_generation_provisioned = frame_generation_provisioned;
            MARK_DIRTY()
        }
        void frameGenerationEnabledUpdated(bool frame_generation_enabled) {
            VALIDATE_AND_GET_PROFILE()
            conf.frame_generation_enabled = frame_generation_enabled;
            MARK_DIRTY()
        }
        void frameGenerationFactorIndexUpdated(uint index) {
            VALIDATE_AND_GET_PROFILE()
            applyFrameGenerationFactorIndex(conf, index);
            MARK_DIRTY()
        }
        void scalingEnabledUpdated(bool scaling_enabled) {
            VALIDATE_AND_GET_PROFILE()
            conf.scaling_enabled = scaling_enabled;
            MARK_DIRTY()
        }
        void swapchainImageCountCompatibilityUpdated(bool enabled) {
            VALIDATE_AND_GET_PROFILE()
            conf.swapchain_image_count_compatibility = enabled;
            MARK_DIRTY()
        }
        void scalingMethodUpdated(const QString& scaling_method) {
            VALIDATE_AND_GET_PROFILE()
            if (conf.scaling_enabled && conf.ultra_performance) return;
            const auto parsed = ls::scalingMethodFromName(
                scaling_method.toStdString()
            );
            if (!parsed) return;
            conf.scaling_method = *parsed;
            MARK_DIRTY()
        }
        void scalingFactorUpdated(float scaling_factor) {
            VALIDATE_AND_GET_PROFILE()
            if (!std::isfinite(scaling_factor)) return;
            conf.scaling_factor = std::clamp(
                scaling_factor,
                ls::GameConfLimits::minimumScalingFactor,
                ls::GameConfLimits::maximumScalingFactor
            );
            MARK_DIRTY()
        }
        void scalingSupersamplingUpdated(bool scaling_supersampling) {
            VALIDATE_AND_GET_PROFILE()
            conf.scaling_supersampling = scaling_supersampling;
            MARK_DIRTY()
        }
        void scalingSharpnessUpdated(float scaling_sharpness) {
            VALIDATE_AND_GET_PROFILE()
            if (!std::isfinite(scaling_sharpness)) return;
            conf.scaling_sharpness = std::clamp(
                scaling_sharpness,
                ls::GameConfLimits::minimumScalingSharpness,
                ls::GameConfLimits::maximumScalingSharpness
            );
            MARK_DIRTY()
        }
        void frameGenerationRefreshThresholdUpdated(
                uint frame_generation_refresh_threshold) {
            VALIDATE_AND_GET_PROFILE()
            conf.frame_generation_refresh_threshold = std::min(
                frame_generation_refresh_threshold,
                ls::GameConfLimits::maximumFrameGenerationRefreshThreshold
            );
            MARK_DIRTY()
        }
        void baseFPSCapUpdated(uint base_fps_cap) {
            VALIDATE_AND_GET_PROFILE()
            conf.base_fps_cap = std::min(
                base_fps_cap,
                static_cast<uint>(ls::GameConfLimits::maximumBaseFpsCap)
            );
            if (conf.base_fps_cap > 0)
                conf.dynamic_cadence_recovery = false;
            MARK_DIRTY()
        }
        void adaptiveUpdated(bool adaptive) {
            VALIDATE_AND_GET_PROFILE()
            conf.adaptive = adaptive;
            MARK_DIRTY()
        }
        void fractionalAdaptiveUpdated(bool fractional_adaptive) {
            VALIDATE_AND_GET_PROFILE()
            applyFractionalAdaptivePreset(conf, fractional_adaptive);
            MARK_DIRTY()
        }
        void adaptiveAutoBaseFPSCapUpdated(bool adaptive_auto_base_fps_cap) {
            VALIDATE_AND_GET_PROFILE()
            conf.adaptive_auto_base_fps_cap = adaptive_auto_base_fps_cap;
            if (adaptive_auto_base_fps_cap)
                conf.dynamic_cadence_recovery = false;
            MARK_DIRTY()
        }
        void adaptiveFractionalRealFramePriorityUpdated(
                const QString& priority) {
            VALIDATE_AND_GET_PROFILE()
            const auto parsed = ls::adaptiveFractionalRealFramePriorityFromName(
                priority.toStdString()
            );
            if (!parsed)
                return;
            conf.adaptive_fractional_real_frame_priority = *parsed;
            conf.dynamic_cadence_recovery = false;
            MARK_DIRTY()
        }
        void targetFPSUpdated(uint target_fps) {
            VALIDATE_AND_GET_PROFILE()
            conf.target_fps = std::clamp(
                target_fps,
                static_cast<uint>(ls::GameConfLimits::minimumTargetFps),
                static_cast<uint>(ls::GameConfLimits::maximumTargetFps)
            );
            MARK_DIRTY()
        }
        void adaptiveMaxMultiplierUpdated(size_t adaptive_max_multiplier) {
            VALIDATE_AND_GET_PROFILE()
            conf.adaptive_max_multiplier = std::clamp(
                adaptive_max_multiplier,
                ls::GameConfLimits::minimumAdaptiveMaxMultiplier,
                ls::GameConfLimits::maximumAdaptiveMaxMultiplier
            );
            MARK_DIRTY()
        }
        void adaptiveStableCadenceUpdated(bool adaptive_stable_cadence) {
            VALIDATE_AND_GET_PROFILE()
            conf.adaptive_stable_cadence = adaptive_stable_cadence;
            MARK_DIRTY()
        }
        void dynamicCadenceRecoveryUpdated(bool dynamic_cadence_recovery) {
            VALIDATE_AND_GET_PROFILE()
            conf.dynamic_cadence_recovery = dynamic_cadence_recovery;
            if (dynamic_cadence_recovery) {
                conf.adaptive_auto_base_fps_cap = false;
                conf.adaptive_fractional_real_frame_priority =
                    ls::AdaptiveFractionalRealFramePriority::Auto;
                conf.base_fps_cap = 0;
            }
            MARK_DIRTY()
        }
        void dynamicCadenceProbeIntervalSecondsUpdated(
                double dynamic_cadence_probe_interval_seconds) {
            VALIDATE_AND_GET_PROFILE()
            conf.dynamic_cadence_probe_interval_seconds = static_cast<float>(
                std::clamp(
                    dynamic_cadence_probe_interval_seconds,
                    static_cast<double>(ls::GameConfLimits::
                        minimumDynamicCadenceProbeIntervalSeconds),
                    static_cast<double>(ls::GameConfLimits::
                        maximumDynamicCadenceProbeIntervalSeconds)
                )
            );
            MARK_DIRTY()
        }
        void ultraPerformanceUpdated(bool ultra_performance) {
            VALIDATE_AND_GET_PROFILE()
            conf.ultra_performance = ultra_performance;
            conf.flow_scale = ultra_performance
                ? ls::GameConfDefaults::ultraPerformanceFlowScale
                : ls::GameConfDefaults::flowScale;
            conf.performance_mode = ultra_performance;
            this->m_global.allow_fp16 = true;
            MARK_DIRTY()
        }
        void flowScaleUpdated(float flow_scale) {
            VALIDATE_AND_GET_PROFILE()
            if (conf.ultra_performance) return;
            conf.flow_scale = flow_scale;
            MARK_DIRTY()
        }
        void performanceModeUpdated(bool performance_mode) {
            VALIDATE_AND_GET_PROFILE()
            if (conf.ultra_performance) return;
            conf.performance_mode = performance_mode;
            MARK_DIRTY()
        }
        void gpuUpdated(int gpu_idx) {
            VALIDATE_AND_GET_PROFILE()
            const auto& gpu = this->m_gpu_list.at(gpu_idx);
            if (gpu.trimmed().isEmpty() || gpu == "Default")
                conf.gpu = std::nullopt;
            else
                conf.gpu.emplace(gpu.toStdString());
            MARK_DIRTY()
        }

        Q_INVOKABLE void addActiveIn(const QString& name) {
            if (name.trimmed().isEmpty()) return;
            VALIDATE_AND_GET_PROFILE()
            auto& active_in = conf.active_in;
            active_in.push_back(name.toStdString());

            auto& model = this->m_active_in_list_models
                .at(static_cast<size_t>(this->m_profile_index));
            model->insertRow(model->rowCount());
            model->setData(model->index(model->rowCount() - 1), name);
            MARK_DIRTY()
        }
        Q_INVOKABLE void removeActiveIn() {
            VALIDATE_AND_GET_PROFILE()
            if (this->m_active_in_index < 0 || std::cmp_greater_equal(static_cast<size_t>(this->m_active_in_index), conf.active_in.size()))
                return;

            auto& active_in = conf.active_in;
            active_in.erase(active_in.begin() + this->m_active_in_index);
            auto& model = this->m_active_in_list_models
                .at(static_cast<size_t>(this->m_profile_index));
            model->removeRow(this->m_active_in_index);
            if (!active_in.empty())
                this->m_active_in_index = 0;
            else
                this->m_active_in_index = -1;
            MARK_DIRTY()
        }

        Q_INVOKABLE void createProfile(const QString& name) {
            if (name.trimmed().isEmpty()) return;

            ls::GameConf conf;
            conf.name = name.toStdString();
            this->m_profiles.push_back(std::move(conf));
            this->m_vkbasalt_profiles.emplace_back();
            this->m_active_in_list_models.push_back(new QStringListModel({}, this));

            auto& model = this->m_profile_list_model;
            model->insertRow(model->rowCount());
            model->setData(model->index(model->rowCount() - 1), name);

            this->m_profile_index = static_cast<int>(this->m_profiles.size() - 1);
            MARK_DIRTY()
        }
        Q_INVOKABLE void renameProfile(const QString& name) {
            if (name.trimmed().isEmpty()) return;

            VALIDATE_AND_GET_PROFILE()
            renameVkBasaltProfile(conf.name, name.toStdString());
            conf.name = name.toStdString();
            auto& model = this->m_profile_list_model;
            model->setData(model->index(this->m_profile_index), name);
            MARK_DIRTY()
        }
        Q_INVOKABLE void deleteProfile() {
            if (!isValidProfileIndex())
                return;

            deleteVkBasaltProfile(static_cast<size_t>(this->m_profile_index));
            auto& profiles = this->m_profiles;
            profiles.erase(profiles.begin() + this->m_profile_index);
            this->m_vkbasalt_profiles.erase(
                this->m_vkbasalt_profiles.begin() + this->m_profile_index
            );
            auto& active_in_models = this->m_active_in_list_models;
            active_in_models.erase(active_in_models.begin() + this->m_profile_index);
            auto& model = this->m_profile_list_model;
            model->removeRow(this->m_profile_index);
            if (!this->m_profiles.empty())
                this->m_profile_index = 0;
            else
                this->m_profile_index = -1;
            MARK_DIRTY()
        }

#undef VALIDATE_AND_GET_PROFILE
#undef MARK_DIRTY
#undef MARK_LAUNCH_DIRTY
#undef MARK_SHADER_DIRTY

    signals:
        void refreshUI();
        void runningGamesChanged();

    private:
        std::filesystem::path m_proc_root;
        std::vector<RunningGame> m_running_games;
        std::unique_ptr<QThread> m_detection_thread;
        bool m_scanning_games{false};
        bool m_capture_failed{false};

        ls::GlobalConf m_global;
        std::vector<ls::GameConf> m_profiles;
        ls::LaunchConf m_launch;
        std::vector<ls::VkBasaltConf> m_vkbasalt_profiles;

        QStringListModel* m_profile_list_model;
        int m_profile_index{-1};

        std::vector<QStringListModel*> m_active_in_list_models;
        int m_active_in_index{-1};

        QStringList m_gpu_list;

        void savePendingChanges();
        std::filesystem::path m_config_path;
        std::filesystem::path m_launch_path;
        std::filesystem::path m_vkbasalt_profile_settings_path;
        std::filesystem::path m_profile_metadata_path;
        std::filesystem::path m_vkbasalt_profile_config_directory;
        std::filesystem::path m_vkbasalt_global_config_path;
        std::filesystem::path m_vkbasalt_shader_source_directory;
        std::filesystem::path m_vkbasalt_shader_directory;
        QJsonObject m_wrapper_settings_root;
        QJsonObject m_profile_metadata_root;
        std::map<std::string, std::string> m_profile_steam_ids;
        QTimer m_save_timer{this};
        bool m_config_dirty{false};
        bool m_launch_dirty{false};
        bool m_vkbasalt_dirty{false};
        bool m_profile_metadata_dirty{false};

        void loadVkBasaltProfiles();
        void writeVkBasaltShaderAssets() const;
        void writeVkBasaltProfiles() const;
        void writeProfileMetadata() const;
        void renameVkBasaltProfile(
            const std::string& oldName,
            const std::string& newName
        );
        void deleteVkBasaltProfile(size_t profileIndex);
        [[nodiscard]] std::filesystem::path vkBasaltConfigPath(
            size_t profileIndex
        ) const;
        [[nodiscard]] std::filesystem::path vkBasaltConfigPathForName(
            const std::string& profileName
        ) const;
    };

}
