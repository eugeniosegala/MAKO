/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <QObject>
#include <QStringListModel>
#include <QString>

#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/launch.hpp"

#include <algorithm>
#include <atomic>
#include <utility>

#define getters public
#define setters public

namespace mako::ui {

    /// Class tying ui and configuration together
    class Backend : public QObject {
        Q_OBJECT

        Q_PROPERTY(QStringListModel* profiles READ calculateProfileListModel NOTIFY refreshUI)
        Q_PROPERTY(int profile_index READ getProfileIndex WRITE profileSelected NOTIFY refreshUI)

        Q_PROPERTY(QString dll READ getDll WRITE dllUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool allow_fp16 READ getAllowFP16 WRITE allowFP16Updated NOTIFY refreshUI)
        Q_PROPERTY(bool enable_zink READ getEnableZink WRITE enableZinkUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool force_alsa_audio READ getForceAlsaAudio WRITE forceAlsaAudioUpdated NOTIFY refreshUI)

        Q_PROPERTY(bool available READ isValidProfileIndex NOTIFY refreshUI)
        Q_PROPERTY(QStringListModel* active_in READ calculateActiveInModel NOTIFY refreshUI)
        Q_PROPERTY(int active_in_index READ getActiveInIndex WRITE activeInSelected NOTIFY refreshUI)
        Q_PROPERTY(QString matched_processes READ getMatchedProcesses NOTIFY refreshUI)
        Q_PROPERTY(size_t multiplier READ getMultiplier WRITE multiplierUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool frame_generation_enabled READ getFrameGenerationEnabled WRITE frameGenerationEnabledUpdated NOTIFY refreshUI)
        Q_PROPERTY(uint frame_generation_refresh_threshold READ getFrameGenerationRefreshThreshold WRITE frameGenerationRefreshThresholdUpdated NOTIFY refreshUI)
        Q_PROPERTY(uint base_fps_cap READ getBaseFPSCap WRITE baseFPSCapUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool adaptive READ getAdaptive WRITE adaptiveUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool adaptive_auto_base_fps_cap READ getAdaptiveAutoBaseFPSCap WRITE adaptiveAutoBaseFPSCapUpdated NOTIFY refreshUI)
        Q_PROPERTY(uint target_fps READ getTargetFPS WRITE targetFPSUpdated NOTIFY refreshUI)
        Q_PROPERTY(size_t adaptive_max_multiplier READ getAdaptiveMaxMultiplier WRITE adaptiveMaxMultiplierUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool adaptive_stable_cadence READ getAdaptiveStableCadence WRITE adaptiveStableCadenceUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool dynamic_cadence_recovery READ getDynamicCadenceRecovery WRITE dynamicCadenceRecoveryUpdated NOTIFY refreshUI)
        Q_PROPERTY(uint dynamic_cadence_probe_interval_seconds READ getDynamicCadenceProbeIntervalSeconds WRITE dynamicCadenceProbeIntervalSecondsUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool ultra_performance READ getUltraPerformance WRITE ultraPerformanceUpdated NOTIFY refreshUI)
        Q_PROPERTY(float flow_scale READ getFlowScale WRITE flowScaleUpdated NOTIFY refreshUI)
        Q_PROPERTY(bool performance_mode READ getPerformanceMode WRITE performanceModeUpdated NOTIFY refreshUI)
        Q_PROPERTY(QStringList gpus READ calculateGPUList NOTIFY refreshUI)
        Q_PROPERTY(int gpu READ getGPU WRITE gpuUpdated NOTIFY refreshUI)

        Q_PROPERTY(uint minimum_multiplier READ getMinimumMultiplier CONSTANT)
        Q_PROPERTY(uint maximum_frame_generation_refresh_threshold READ getMaximumFrameGenerationRefreshThreshold CONSTANT)
        Q_PROPERTY(uint frame_generation_refresh_threshold_preset READ getFrameGenerationRefreshThresholdPreset CONSTANT)
        Q_PROPERTY(uint minimum_base_fps_cap READ getMinimumBaseFPSCap CONSTANT)
        Q_PROPERTY(uint maximum_base_fps_cap READ getMaximumBaseFPSCap CONSTANT)
        Q_PROPERTY(uint minimum_target_fps READ getMinimumTargetFPS CONSTANT)
        Q_PROPERTY(uint maximum_target_fps READ getMaximumTargetFPS CONSTANT)
        Q_PROPERTY(uint minimum_adaptive_max_multiplier READ getMinimumAdaptiveMaxMultiplier CONSTANT)
        Q_PROPERTY(uint maximum_adaptive_max_multiplier READ getMaximumAdaptiveMaxMultiplier CONSTANT)
        Q_PROPERTY(uint minimum_dynamic_cadence_probe_interval_seconds READ getMinimumDynamicCadenceProbeIntervalSeconds CONSTANT)
        Q_PROPERTY(uint maximum_dynamic_cadence_probe_interval_seconds READ getMaximumDynamicCadenceProbeIntervalSeconds CONSTANT)
        Q_PROPERTY(float minimum_flow_scale READ getMinimumFlowScale CONSTANT)
        Q_PROPERTY(float maximum_flow_scale READ getMaximumFlowScale CONSTANT)

    public:
        explicit Backend();

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
        [[nodiscard]] bool getFrameGenerationEnabled() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::frameGenerationEnabled)
            return conf.frame_generation_enabled;
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
        [[nodiscard]] bool getAdaptiveAutoBaseFPSCap() const {
            VALIDATE_AND_GET_PROFILE(ls::GameConfDefaults::adaptiveAutoBaseFpsCap)
            return conf.adaptive_auto_base_fps_cap;
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
        [[nodiscard]] uint getDynamicCadenceProbeIntervalSeconds() const {
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
        [[nodiscard]] uint getMinimumDynamicCadenceProbeIntervalSeconds()
                const noexcept {
            return ls::GameConfLimits::
                minimumDynamicCadenceProbeIntervalSeconds;
        }
        [[nodiscard]] uint getMaximumDynamicCadenceProbeIntervalSeconds()
                const noexcept {
            return ls::GameConfLimits::
                maximumDynamicCadenceProbeIntervalSeconds;
        }
        [[nodiscard]] float getMinimumFlowScale() const noexcept {
            return ls::GameConfLimits::minimumFlowScale;
        }
        [[nodiscard]] float getMaximumFlowScale() const noexcept {
            return ls::GameConfLimits::maximumFlowScale;
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
    this->m_config_dirty.store(true, std::memory_order_relaxed); \
    emit refreshUI();

#define MARK_LAUNCH_DIRTY() \
    this->m_launch_dirty.store(true, std::memory_order_relaxed); \
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

#define VALIDATE_AND_GET_PROFILE() \
    if (!isValidProfileIndex()) return; \
    auto& conf = this->m_profiles.at(static_cast<size_t>(this->m_profile_index));

        void multiplierUpdated(size_t multiplier) {
            VALIDATE_AND_GET_PROFILE()
            conf.multiplier = multiplier;
            MARK_DIRTY()
        }
        void frameGenerationEnabledUpdated(bool frame_generation_enabled) {
            VALIDATE_AND_GET_PROFILE()
            conf.frame_generation_enabled = frame_generation_enabled;
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
        void adaptiveAutoBaseFPSCapUpdated(bool adaptive_auto_base_fps_cap) {
            VALIDATE_AND_GET_PROFILE()
            conf.adaptive_auto_base_fps_cap = adaptive_auto_base_fps_cap;
            if (adaptive_auto_base_fps_cap)
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
                conf.base_fps_cap = 0;
            }
            MARK_DIRTY()
        }
        void dynamicCadenceProbeIntervalSecondsUpdated(
                uint dynamic_cadence_probe_interval_seconds) {
            VALIDATE_AND_GET_PROFILE()
            conf.dynamic_cadence_probe_interval_seconds = std::clamp(
                dynamic_cadence_probe_interval_seconds,
                ls::GameConfLimits::
                    minimumDynamicCadenceProbeIntervalSeconds,
                ls::GameConfLimits::
                    maximumDynamicCadenceProbeIntervalSeconds
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
            conf.name = name.toStdString();
            auto& model = this->m_profile_list_model;
            model->setData(model->index(this->m_profile_index), name);
            MARK_DIRTY()
        }
        Q_INVOKABLE void deleteProfile() {
            if (!isValidProfileIndex())
                return;

            auto& profiles = this->m_profiles;
            profiles.erase(profiles.begin() + this->m_profile_index);
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

    signals:
        void refreshUI();

    private:
        ls::GlobalConf m_global;
        std::vector<ls::GameConf> m_profiles;
        ls::LaunchConf m_launch;

        QStringListModel* m_profile_list_model;
        int m_profile_index{-1};

        std::vector<QStringListModel*> m_active_in_list_models;
        int m_active_in_index{-1};

        QStringList m_gpu_list;

        std::atomic_bool m_config_dirty{false};
        std::atomic_bool m_launch_dirty{false};
    };

}
