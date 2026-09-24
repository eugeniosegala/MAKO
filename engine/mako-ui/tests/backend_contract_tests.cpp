/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "backend.hpp"
#include "utils.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMetaProperty>
#include <QString>
#include <QTemporaryDir>

#include <cstring>
#include <iostream>
#include <stdexcept>

// GPU discovery is unrelated to persistence and must not require a Vulkan host.
QStringList mako::ui::getAvailableGPUs() {
    return {QStringLiteral("Default")};
}

namespace {

void require(const bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void require_property(const char* name, const char* type_name,
        const bool writable, const bool constant) {
    const QMetaObject& meta_object = mako::ui::Backend::staticMetaObject;
    const int property_index = meta_object.indexOfProperty(name);
    require(property_index >= 0, "required Qt backend property is missing");

    const QMetaProperty property = meta_object.property(property_index);
    require(std::strcmp(property.typeName(), type_name) == 0,
        "Qt backend property has the wrong type");
    require(property.isReadable(), "Qt backend property is not readable");
    require(property.isWritable() == writable,
        "Qt backend property has the wrong writable contract");
    require(property.isConstant() == constant,
        "Qt backend property has the wrong constant contract");
    require(constant || property.hasNotifySignal(),
        "writable Qt backend property has no notification signal");
}

void test_scaling_properties() {
    require_property("running_games", "QVariantList", false, false);
    require_property("scanning_games", "bool", false, false);
    require_property("capture_failed", "bool", false, false);
    static_assert(!ls::GameConfDefaults::scalingEnabled);
    static_assert(ls::GameConfDefaults::scalingMethod ==
        ls::ScalingMethod::Ls1);
    static_assert(ls::GameConfDefaults::scalingFactor == 1.5F);
    static_assert(!ls::GameConfDefaults::scalingSupersampling);
    static_assert(ls::GameConfDefaults::scalingSharpness == 0.8F);
    static_assert(ls::GameConfLimits::minimumScalingFactor == 1.0F);
    static_assert(ls::GameConfLimits::maximumScalingFactor == 2.0F);
    static_assert(ls::GameConfLimits::minimumScalingSharpness == 0.0F);
    static_assert(ls::GameConfLimits::maximumScalingSharpness == 1.0F);
    ls::GameConf ultra_scaling;
    ultra_scaling.scaling_enabled = true;
    ultra_scaling.scaling_method = ls::ScalingMethod::Mako;
    ultra_scaling.ultra_performance = true;
    require(ls::effectiveScalingMethod(ultra_scaling) ==
            ls::ScalingMethod::Ls1Performance,
        "Ultra Performance does not select LS1 Performance for Scaling");

    require_property("scaling_enabled", "bool", true, false);
    require_property(
        "swapchain_image_count_compatibility", "bool", true, false
    );
    require_property("scaling_method", "QString", true, false);
    require_property("scaling_factor", "float", true, false);
    require_property("scaling_supersampling", "bool", true, false);
    require_property("scaling_sharpness", "float", true, false);
    require_property("minimum_scaling_factor", "float", false, true);
    require_property("maximum_scaling_factor", "float", false, true);
    require_property("minimum_scaling_sharpness", "float", false, true);
    require_property("maximum_scaling_sharpness", "float", false, true);
}

void test_multiplier_limits() {
    static_assert(ls::GameConfLimits::minimumMultiplier == 2);
    static_assert(ls::GameConfLimits::maximumMultiplier == 5);
    static_assert(ls::GameConfLimits::minimumAdaptiveMaxMultiplier == 2);
    static_assert(ls::GameConfLimits::maximumAdaptiveMaxMultiplier == 5);

    require_property("minimum_multiplier", "uint", false, true);
    require_property("maximum_multiplier", "uint", false, true);
    require_property("minimum_adaptive_max_multiplier", "uint", false, true);
    require_property("maximum_adaptive_max_multiplier", "uint", false, true);
    require_property("frame_generation_provisioned", "bool", true, false);
    require_property("frame_generation_enabled", "bool", true, false);
    require_property("frame_generation_factor_index", "uint", true, false);

    ls::GameConf configuration;
    require(mako::ui::Backend::frameGenerationFactorIndex(configuration) == 1,
        "Default Fixed 2x did not map to the first active factor choice");
    mako::ui::Backend::applyFrameGenerationFactorIndex(configuration, 0);
    require(!configuration.frame_generation_enabled &&
            configuration.multiplier == 2,
        "0x did not pause Fixed generation while preserving its multiplier");
    mako::ui::Backend::applyFrameGenerationFactorIndex(configuration, 4);
    require(configuration.frame_generation_enabled &&
            configuration.multiplier == 5,
        "The highest Fixed factor choice did not select 5x");
    configuration.adaptive = true;
    configuration.adaptive_max_multiplier = 4;
    mako::ui::Backend::applyFrameGenerationFactorIndex(configuration, 0);
    require(!configuration.frame_generation_enabled &&
            configuration.adaptive && configuration.multiplier == 5 &&
            configuration.adaptive_max_multiplier == 4,
        "Adaptive 0x did not preserve its mode and saved multipliers");
    mako::ui::Backend::applyFrameGenerationFactorIndex(configuration, 3);
    require(configuration.frame_generation_enabled &&
            configuration.adaptive && configuration.multiplier == 5 &&
            configuration.adaptive_max_multiplier == 4 &&
            mako::ui::Backend::frameGenerationFactorIndex(configuration) == 3,
        "Adaptive could not resume at its selected maximum multiplier");

    QFile file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString qml = QString::fromUtf8(file.readAll());
    require(qml.contains(QStringLiteral(
            "currentIndex: backend.frame_generation_factor_index")) &&
            qml.contains(QStringLiteral(
                "onActivated: index => backend.frame_generation_factor_index = index")) &&
            qml.contains(QStringLiteral(
                "[\"0\" + t.multiplierX, \"2\" + t.multiplierX, \"3\" + t.multiplierX, \"4\" + t.multiplierX, \"5\" + t.multiplierX]")),
        "Frame Generation multipliers do not expose the shared 0x and 2x-5x choices");
    require(qml.contains(QStringLiteral(
            "visible: backend.frame_generation_provisioned && backend.adaptive")) &&
            qml.contains(QStringLiteral(
                "visible: backend.frame_generation_provisioned && !backend.adaptive")),
        "Qt does not switch between the Adaptive and Fixed multiplier controls");
}

void test_fractional_adaptive_preset() {
    require_property("fractional_adaptive", "bool", true, false);
    require_property(
        "adaptive_fractional_real_frame_priority", "QString", true, false
    );
    require_property(
        "adaptive_fractional_real_frame_priority_cap", "double", false, false
    );
    require(ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::Auto, 120) == 0.0 &&
            ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::Low, 120) == 72.0 &&
            ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::Medium, 120) == 80.0 &&
            ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::High, 120) == 90.0 &&
            ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::VeryHigh, 120) == 96.0,
        "Fractional real-frame priorities lost their cadence-friendly ratios");

    ls::GameConf configuration;
    configuration.frame_generation_enabled = false;
    configuration.adaptive = false;
    configuration.adaptive_auto_base_fps_cap = true;
    configuration.dynamic_cadence_recovery = true;
    mako::ui::Backend::applyFractionalAdaptivePreset(configuration, true);
    require(!configuration.frame_generation_enabled,
        "Editing Fractional Adaptive unexpectedly resumed Frame Generation from 0x");
    require(configuration.adaptive,
        "Fractional Adaptive did not enable Adaptive Frame Generation");
    require(!configuration.adaptive_auto_base_fps_cap,
        "Fractional Adaptive did not disable Steady Base Cap");
    require(!configuration.dynamic_cadence_recovery,
        "Fractional Adaptive did not disable Dynamic Cadence Recovery");
    require(mako::ui::Backend::isFractionalAdaptivePresetEnabled(configuration),
        "Fractional Adaptive state was not recognized after enabling it");

    configuration.dynamic_cadence_recovery = true;
    mako::ui::Backend::applyFractionalAdaptivePreset(configuration, false);
    require(configuration.adaptive_auto_base_fps_cap,
        "Disabling Fractional Adaptive did not restore Steady Base Cap");
    require(!configuration.dynamic_cadence_recovery,
        "Disabling Fractional Adaptive did not disable Dynamic Cadence Recovery");
    require(!mako::ui::Backend::isFractionalAdaptivePresetEnabled(configuration),
        "Fractional Adaptive state remained enabled after disabling it");

    QFile file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString qml = QString::fromUtf8(file.readAll());
    require(qml.contains(QStringLiteral("title: t.fractionalAdaptive")),
        "Fractional Adaptive control is missing from the Renderer UI");
    require(qml.contains(QStringLiteral("checked: backend.fractional_adaptive")),
        "Fractional Adaptive control does not read the atomic preset property");
    require(qml.contains(QStringLiteral(
            "onToggled: backend.fractional_adaptive = checked")),
        "Fractional Adaptive control does not update the atomic preset property");
    require(qml.contains(QStringLiteral(
            "visible: backend.frame_generation_provisioned\n"
            "                            && backend.fractional_adaptive")) &&
            qml.contains(QStringLiteral(
                "model: [t.automatic, t.low, t.medium, t.high, t.veryHigh]")) &&
            qml.contains(QStringLiteral(
                "backend.adaptive_fractional_real_frame_priority !== \"auto\"")),
        "Fractional priority is not scoped to Fractional Adaptive or does not own its explicit cap");
}

void test_feature_group_order_and_ownership() {
    QFile file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString qml = QString::fromUtf8(file.readAll());

    const qsizetype group_start = qml.indexOf(
        QStringLiteral("name: t.scalingSettings")
    );
    require(group_start >= 0, "independent Scaling group is missing");
    const qsizetype frame_generation_group_start = qml.indexOf(
        QStringLiteral("name: t.frameGeneration")
    );
    require(frame_generation_group_start >= 0,
        "Frame Generation group is missing");
    const qsizetype performance_group_start = qml.indexOf(
        QStringLiteral("name: t.performanceSettings")
    );
    require(performance_group_start >= 0,
        "Performance Settings group is missing");
    require(frame_generation_group_start < group_start &&
            group_start < performance_group_start,
        "Frame Generation, Scaling, and Performance Settings are out of order");

    const QString frame_generation_group = qml.mid(
        frame_generation_group_start,
        group_start - frame_generation_group_start
    );
    require(frame_generation_group.count(QStringLiteral("GroupEntry {")) == 13 &&
            frame_generation_group.count(QStringLiteral(
                "visible: backend.frame_generation_provisioned")) == 12,
        "Frame Generation must retain its provisioning switch while keeping live controls visible at 0x");
    require(frame_generation_group.contains(QStringLiteral(
                "checked: backend.frame_generation_provisioned")) &&
            frame_generation_group.contains(QStringLiteral(
                "onToggled: backend.frame_generation_provisioned = checked")),
        "Frame Generation provisioning is not exposed as the restart-bound switch");
    require(!frame_generation_group.contains(QStringLiteral(
                "enabled: backend.frame_generation_enabled")),
        "A dormant live Frame Generation setting cannot be prepared while the factor is 0x");
    require(frame_generation_group.contains(
                QStringLiteral("title: t.performanceMode")) &&
            frame_generation_group.contains(
                QStringLiteral("checked: backend.performance_mode")),
        "Lighter FG Model does not belong to Frame Generation");
    const qsizetype fixed_multiplier_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.multiplier")
    );
    const qsizetype adaptive_multiplier_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.maxAdaptiveMultiplier")
    );
    const qsizetype smooth_cadence_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.smoothCadence")
    );
    const qsizetype lighter_model_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.performanceMode")
    );
    require(adaptive_multiplier_entry >= 0 &&
            adaptive_multiplier_entry < fixed_multiplier_entry &&
            fixed_multiplier_entry < smooth_cadence_entry &&
            smooth_cadence_entry < lighter_model_entry,
        "Frame Generation must order its conditional multipliers, Smooth Cadence, then Lighter FG Model");

    qsizetype performance_group_end = qml.indexOf(
        QStringLiteral("\n                Group {"),
        performance_group_start + 1
    );
    if (performance_group_end < 0)
        performance_group_end = qml.size();
    const QString performance_group = qml.mid(
        performance_group_start,
        performance_group_end - performance_group_start
    );
    require(!performance_group.contains(
                QStringLiteral("title: t.performanceMode")),
        "Lighter FG Model remains duplicated under Performance Settings");

    qsizetype group_end = qml.indexOf(
        QStringLiteral("\n                Group {"), group_start + 1
    );
    if (group_end < 0)
        group_end = qml.size();
    const QString scaling_group = qml.mid(group_start, group_end - group_start);

    require(scaling_group.contains(QStringLiteral("backend.scaling_enabled")),
        "Scaling group does not bind the enable property");
    require(scaling_group.contains(QStringLiteral("backend.scaling_method")),
        "Scaling group does not bind the method property");
    require(scaling_group.contains(QStringLiteral("backend.scaling_factor")),
        "Scaling group does not bind the factor property");
    require(scaling_group.contains(
                QStringLiteral("backend.scaling_supersampling")),
        "Scaling group does not bind the supersampling property");
    require(scaling_group.contains(QStringLiteral("backend.scaling_sharpness")),
        "Scaling group does not bind the sharpness property");
    require(scaling_group.count(
            QStringLiteral("visible: backend.scaling_enabled")) == 4 &&
            scaling_group.count(QStringLiteral(
                "visible: backend.scaling_enabled && backend.scaling_method !== \"native\"")) == 1,
        "Native must retain Scale Factor while hiding model-only sharpness");
    require(scaling_group.contains(QStringLiteral(
            "model: [t.scalingMethodNative, t.scalingMethodMako, t.scalingMethodLs1, t.scalingMethodLs1Performance]")),
        "Scaling method order must expose Native before every scaler");
    require(scaling_group.contains(QStringLiteral(
            "enabled: backend.scaling_enabled && !backend.ultra_performance")),
        "Ultra Performance must visibly lock the Renderer scaling method");
    require(scaling_group.contains(QStringLiteral(
            "currentIndex: backend.ultra_performance ? 3")),
        "Ultra Performance must visibly select LS1 Performance");
    require(!scaling_group.contains(QStringLiteral("backend.adaptive")),
        "Scaling group is coupled to Adaptive");
    require(!scaling_group.contains(
            QStringLiteral("backend.frame_generation_enabled")),
        "Scaling group is coupled to Frame Generation");
    require(!scaling_group.contains(
            QStringLiteral("backend.frame_generation_provisioned")),
        "Scaling group is coupled to Frame Generation provisioning");

    const qsizetype compatibility_group_start = qml.indexOf(
        QStringLiteral("name: t.compatibilitySettings")
    );
    require(compatibility_group_start >= 0,
        "Compatibility Settings group is missing");
    qsizetype compatibility_group_end = qml.indexOf(
        QStringLiteral("\n                Group {"),
        compatibility_group_start + 1
    );
    if (compatibility_group_end < 0)
        compatibility_group_end = qml.size();
    const QString compatibility_group = qml.mid(
        compatibility_group_start,
        compatibility_group_end - compatibility_group_start
    );
    require(compatibility_group.contains(QStringLiteral(
                "checked: backend.swapchain_image_count_compatibility")) &&
            compatibility_group.contains(QStringLiteral(
                "onToggled: backend.swapchain_image_count_compatibility = checked")),
        "Compatibility Settings does not expose the restart-bound swapchain image policy");
}

void test_shader_controls() {
    require_property("enable_vkbasalt", "bool", true, false);
    require_property("vkbasalt_sharpening", "QString", true, false);
    require_property("vkbasalt_sharpness", "float", true, false);
    require_property("vkbasalt_dls_denoise", "float", true, false);
    require_property("vkbasalt_antialiasing", "QString", true, false);
    require_property("vkbasalt_shader", "QString", true, false);
    require_property("vkbasalt_config_path", "QString", false, false);
    require(mako::ui::Backend::staticMetaObject.indexOfMethod(
                "openVkBasaltConfig()") >= 0,
        "Qt backend does not expose the advanced shader configuration opener");
    require_property("launch_option", "QString", false, false);
    require_property("minimum_vkbasalt_strength", "float", false, true);
    require_property("maximum_vkbasalt_strength", "float", false, true);

    QFile file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString qml = QString::fromUtf8(file.readAll());
    require(qml.contains(QStringLiteral("name: t.shaderSettings")) &&
            qml.contains(QStringLiteral("checked: backend.enable_vkbasalt")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_shader")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_sharpening")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_sharpness")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_dls_denoise")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_antialiasing")) &&
            qml.contains(QStringLiteral(
                "onClicked: backend.openVkBasaltConfig()")) &&
            qml.contains(QStringLiteral("text: backend.vkbasalt_config_path")),
        "Qt shader group does not expose the Decky-compatible compact controls");
}

void test_compact_restart_markers() {
    QFile ui_file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(ui_file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString ui_qml = QString::fromUtf8(ui_file.readAll());
    require(ui_qml.count(QStringLiteral("compactRestartMarker: true")) == 10,
        "Every restart-bound Renderer control must opt into the compact marker");

    QFile entry_file(QString::fromUtf8(MAKO_UI_GROUP_ENTRY_QML_FILE));
    require(entry_file.open(QIODevice::ReadOnly),
        "MAKO GroupEntry QML could not be opened");
    const QString entry_qml = QString::fromUtf8(entry_file.readAll());
    require(entry_qml.contains(QStringLiteral(
            "property bool compactRestartMarker: false")),
        "GroupEntry does not expose the compact restart-marker contract");
    require(entry_qml.contains(QStringLiteral("font-size: 72%")),
        "Restart marker is not rendered at the compact size");
}

void test_save_lifetime() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary configuration directory failed");
    const auto configPath = directory.filePath("conf.toml").toStdString();
    const auto launchPath = directory.filePath("launcher.conf").toStdString();
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const auto previousShaderDirectory = qgetenv("MAKO_VKBASALT_SHADER_DIR");
    qputenv("MAKO_CONFIG", QByteArray::fromStdString(configPath));
    qputenv("MAKO_LAUNCH_CONFIG", QByteArray::fromStdString(launchPath));
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("MAKO_VKBASALT_SHADER_DIR", directory.path().toUtf8());
    {
        mako::ui::Backend backend;
        backend.targetFPSUpdated(90);
        backend.targetFPSUpdated(144);
        backend.enableZinkUpdated(true);
        backend.enableVkBasaltUpdated(true);
        backend.vkBasaltSharpeningUpdated(QStringLiteral("dls"));
        backend.vkBasaltSharpnessUpdated(0.75F);
        backend.vkBasaltDlsDenoiseUpdated(0.4F);
        backend.vkBasaltAntialiasingUpdated(QStringLiteral("fxaa"));
        backend.vkBasaltShaderUpdated(QStringLiteral("vibrance"));
        require(!std::filesystem::exists(configPath), "UI edit was not debounced");
        QEventLoop events;
        QTimer::singleShot(700, &events, &QEventLoop::quit);
        events.exec();
        require(ls::ConfigFile(configPath).profiles().front().target_fps == 144,
            "UI timer did not save the latest edit");
        require(ls::LaunchConfigFile(launchPath).settings().enable_zink,
            "UI timer did not save launcher settings");
        const auto vkBasaltPath = directory.filePath(
            "vkbasalt/profile-"
        ).toStdString();
        require(backend.getVkBasaltConfigPath().startsWith(
                    QString::fromStdString(vkBasaltPath)) &&
                std::filesystem::exists(
                    backend.getVkBasaltConfigPath().toStdString()),
            "UI did not create the selected profile's vkBasalt configuration");
        const auto selectedProfile = backend.calculateProfileListModel()
            ->data(backend.calculateProfileListModel()->index(0, 0)).toString();
        require(backend.getLaunchOption().contains(
                    QStringLiteral("ENABLE_VKBASALT=1")) &&
                backend.getLaunchOption().contains(
                    QStringLiteral("MAKO_PROFILE='") + selectedProfile +
                    QStringLiteral("'")),
            "UI did not produce the profile-specific shader launch option");
        QFile sidecar(directory.filePath(
            "profile-wrapper-settings.json"
        ));
        require(sidecar.open(QIODevice::ReadOnly) &&
                sidecar.readAll().contains("\"external_vulkan_layer\": \"vkbasalt\""),
            "UI did not reuse Decky's profile wrapper settings sidecar");
        const auto timestamp = std::filesystem::last_write_time(configPath);
        QTimer::singleShot(700, &events, &QEventLoop::quit);
        events.exec();
        require(std::filesystem::last_write_time(configPath) == timestamp,
            "Idle UI rewrote the configuration");
        backend.targetFPSUpdated(165);
        backend.forceAlsaAudioUpdated(true);
        // No event loop: closing before the debounce must still save both files.
    }
    require(ls::ConfigFile(configPath).profiles().front().target_fps == 165,
        "Closing the UI lost the pending profile edit");
    require(ls::LaunchConfigFile(launchPath).settings().force_alsa_audio,
        "Closing the UI lost the pending launcher edit");
    QFile invalid(QString::fromStdString(configPath));
    require(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate), "Invalid fixture could not be written");
    invalid.write("version = [broken");
    invalid.close();
    QFile backup(QString::fromStdString(configPath + ".old"));
    require(backup.open(QIODevice::WriteOnly), "Backup fixture could not be written");
    backup.write("previous backup");
    backup.close();
    bool refused = false;
    try { mako::ui::Backend backend; }
    catch (const std::exception&) { refused = true; }
    require(refused, "UI accepted an invalid configuration without preserving its backup");
    require(invalid.open(QIODevice::ReadOnly) && invalid.readAll() == "version = [broken",
        "UI changed the invalid configuration after backup failed");
    require(backup.open(QIODevice::ReadOnly) && backup.readAll() == "previous backup",
        "UI overwrote an earlier configuration backup");
    if (previousConfig.isNull()) qunsetenv("MAKO_CONFIG");
    else qputenv("MAKO_CONFIG", previousConfig);
    if (previousLaunch.isNull()) qunsetenv("MAKO_LAUNCH_CONFIG");
    else qputenv("MAKO_LAUNCH_CONFIG", previousLaunch);
    if (previousConfigHome.isNull()) qunsetenv("XDG_CONFIG_HOME");
    else qputenv("XDG_CONFIG_HOME", previousConfigHome);
    if (previousShaderDirectory.isNull()) qunsetenv("MAKO_VKBASALT_SHADER_DIR");
    else qputenv("MAKO_VKBASALT_SHADER_DIR", previousShaderDirectory);
}

void test_decky_shader_profile_round_trip_and_owned_deletion() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary configuration directory failed");
    const auto configPath = directory.filePath("conf.toml").toStdString();
    const auto launchPath = directory.filePath("launcher.conf").toStdString();
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const auto previousShaderDirectory = qgetenv("MAKO_VKBASALT_SHADER_DIR");
    qputenv("MAKO_CONFIG", QByteArray::fromStdString(configPath));
    qputenv("MAKO_LAUNCH_CONFIG", QByteArray::fromStdString(launchPath));
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("MAKO_VKBASALT_SHADER_DIR", directory.path().toUtf8());

    ls::ConfigFile config;
    config.profiles().front().name = "decky-game";
    ls::GameConf unrelatedProfile;
    unrelatedProfile.name = "other-game";
    config.profiles().push_back(unrelatedProfile);
    config.write(configPath);

    QFile sidecar(directory.filePath("profile-wrapper-settings.json"));
    require(sidecar.open(QIODevice::WriteOnly),
        "Decky wrapper settings fixture could not be written");
    sidecar.write(R"JSON({
  "version": 1,
  "profiles": {
    "decky-game": {
      "external_vulkan_layer": "vkbasalt",
      "vkbasalt_sharpening": "dls",
      "vkbasalt_sharpness": 0.65,
      "vkbasalt_dls_denoise": 0.35,
      "vkbasalt_antialiasing": "smaa",
      "vkbasalt_shader": "technicolor2",
      "future_setting": "keep"
    },
    "other-game": {
      "external_vulkan_layer": "",
      "vkbasalt_sharpening": "cas",
      "vkbasalt_sharpness": 0.5,
      "vkbasalt_dls_denoise": 0.2,
      "vkbasalt_antialiasing": "none",
      "vkbasalt_shader": "none",
      "unrelated": true
    }
  }
})JSON");
    sidecar.close();

    QFile metadata(directory.filePath("profile-metadata.json"));
    require(metadata.open(QIODevice::WriteOnly),
        "Decky profile metadata fixture could not be written");
    metadata.write(R"JSON({
  "version": 1,
  "profiles": {
    "decky-game": {"display_name": "Decky Game", "kind": "steam", "steam_app_id": "111", "captured_processes": []},
    "other-game": {"display_name": "Other Game", "kind": "steam", "steam_app_id": "222", "captured_processes": []}
  }
})JSON");
    metadata.close();

    const auto profileDirectory = directory.filePath("vkbasalt");
    require(QDir().mkpath(profileDirectory),
        "profile shader directory fixture could not be created");
    QFile ownedShader(profileDirectory + QStringLiteral("/steam-111.conf"));
    require(ownedShader.open(QIODevice::WriteOnly),
        "owned shader fixture could not be written");
    ownedShader.write("effects = custom\n");
    ownedShader.close();
    QFile unrelatedShader(profileDirectory + QStringLiteral("/steam-222.conf"));
    require(unrelatedShader.open(QIODevice::WriteOnly),
        "unrelated shader fixture could not be written");
    unrelatedShader.write("effects = unrelated\n");
    unrelatedShader.close();
    const auto globalShaderPath = directory.filePath("vkBasalt/vkBasalt.conf");
    require(QDir().mkpath(QFileInfo(globalShaderPath).path()),
        "global shader directory fixture could not be created");
    QFile globalShader(globalShaderPath);
    require(globalShader.open(QIODevice::WriteOnly),
        "global shader fixture could not be written");
    globalShader.write("effects = global\n");
    globalShader.close();

    {
        mako::ui::Backend backend;
        require(backend.getEnableVkBasalt() &&
                backend.getVkBasaltSharpening() == QStringLiteral("dls") &&
                std::abs(backend.getVkBasaltSharpness() - 0.65F) < 0.001F &&
                std::abs(backend.getVkBasaltDlsDenoise() - 0.35F) < 0.001F &&
                backend.getVkBasaltAntialiasing() == QStringLiteral("smaa") &&
                backend.getVkBasaltShader() == QStringLiteral("technicolor2"),
            "Qt did not load the selected profile's existing Decky shader settings");
        require(backend.getVkBasaltConfigPath().endsWith(
                    QStringLiteral("/steam-111.conf")),
            "Qt did not reuse Decky's Steam-aware shader config identity");
        backend.deleteProfile();
        QEventLoop events;
        QTimer::singleShot(700, &events, &QEventLoop::quit);
        events.exec();
    }

    require(!QFileInfo::exists(profileDirectory + QStringLiteral("/steam-111.conf")),
        "deleting the main profile left its attached shader config behind");
    require(QFileInfo::exists(profileDirectory + QStringLiteral("/steam-222.conf")) &&
            QFileInfo::exists(globalShaderPath),
        "deleting one profile removed an unrelated or global shader config");

    require(sidecar.open(QIODevice::ReadOnly),
        "updated Decky wrapper settings could not be read");
    const auto storedSettings = QJsonDocument::fromJson(sidecar.readAll())
        .object().value(QStringLiteral("profiles")).toObject();
    require(!storedSettings.contains(QStringLiteral("decky-game")) &&
            storedSettings.value(QStringLiteral("other-game")).toObject()
                .value(QStringLiteral("unrelated")).toBool(),
        "profile deletion changed Decky settings outside the attached profile");
    sidecar.close();
    require(metadata.open(QIODevice::ReadOnly),
        "updated Decky metadata could not be read");
    const auto storedMetadata = QJsonDocument::fromJson(metadata.readAll())
        .object().value(QStringLiteral("profiles")).toObject();
    require(!storedMetadata.contains(QStringLiteral("decky-game")) &&
            storedMetadata.contains(QStringLiteral("other-game")),
        "profile deletion changed Decky metadata outside the attached profile");

    if (previousConfig.isNull()) qunsetenv("MAKO_CONFIG");
    else qputenv("MAKO_CONFIG", previousConfig);
    if (previousLaunch.isNull()) qunsetenv("MAKO_LAUNCH_CONFIG");
    else qputenv("MAKO_LAUNCH_CONFIG", previousLaunch);
    if (previousConfigHome.isNull()) qunsetenv("XDG_CONFIG_HOME");
    else qputenv("XDG_CONFIG_HOME", previousConfigHome);
    if (previousShaderDirectory.isNull()) qunsetenv("MAKO_VKBASALT_SHADER_DIR");
    else qputenv("MAKO_VKBASALT_SHADER_DIR", previousShaderDirectory);
}

} // namespace

int main(int argc, char* argv[]) {
    const QCoreApplication application(argc, argv);
    try {
        test_scaling_properties();
        test_multiplier_limits();
        test_fractional_adaptive_preset();
        test_feature_group_order_and_ownership();
        test_shader_controls();
        test_compact_restart_markers();
        test_save_lifetime();
        test_decky_shader_profile_round_trip_and_owned_deletion();
    } catch (const std::exception& error) {
        std::cerr << "mako-ui backend contract test failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
