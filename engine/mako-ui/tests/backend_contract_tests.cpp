/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "backend.hpp"

#include <QFile>
#include <QMetaObject>
#include <QMetaProperty>
#include <QString>

#include <cstring>
#include <iostream>
#include <stdexcept>

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
    static_assert(!ls::GameConfDefaults::scalingEnabled);
    static_assert(ls::GameConfDefaults::scalingMethod ==
        ls::ScalingMethod::Native);
    static_assert(ls::GameConfDefaults::scalingFactor == 2.0F);
    static_assert(ls::GameConfDefaults::scalingSharpness == 0.9F);
    static_assert(ls::GameConfLimits::minimumScalingFactor == 1.0F);
    static_assert(ls::GameConfLimits::maximumScalingFactor == 2.0F);
    static_assert(ls::GameConfLimits::minimumScalingSharpness == 0.0F);
    static_assert(ls::GameConfLimits::maximumScalingSharpness == 1.0F);

    require_property("scaling_enabled", "bool", true, false);
    require_property("scaling_method", "QString", true, false);
    require_property("scaling_factor", "float", true, false);
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

    QFile file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString qml = QString::fromUtf8(file.readAll());
    require(qml.contains(QStringLiteral("to: backend.maximum_multiplier")),
        "Fixed multiplier spin box does not expose the Renderer maximum");
    require(qml.contains(QStringLiteral(
            "to: backend.maximum_adaptive_max_multiplier")),
        "Adaptive multiplier spin box does not expose the Renderer maximum");
}

void test_fractional_adaptive_preset() {
    require_property("fractional_adaptive", "bool", true, false);

    ls::GameConf configuration;
    configuration.frame_generation_enabled = false;
    configuration.adaptive = false;
    configuration.adaptive_auto_base_fps_cap = true;
    configuration.dynamic_cadence_recovery = true;
    mako::ui::Backend::applyFractionalAdaptivePreset(configuration, true);
    require(configuration.frame_generation_enabled,
        "Fractional Adaptive did not enable Frame Generation");
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
}

void test_independent_scaling_group() {
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
    require(group_start < frame_generation_group_start,
        "Scaling must precede Frame Generation to match MAKO Decky");
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
    require(scaling_group.contains(QStringLiteral("backend.scaling_sharpness")),
        "Scaling group does not bind the sharpness property");
    require(scaling_group.count(
            QStringLiteral("visible: backend.scaling_enabled")) == 3 &&
            scaling_group.count(QStringLiteral(
                "visible: backend.scaling_enabled && backend.scaling_method !== \"native\"")) == 1,
        "Native must retain Scale Factor while hiding model-only sharpness");
    require(scaling_group.contains(QStringLiteral(
            "model: [t.scalingMethodNative, t.scalingMethodMako, t.scalingMethodLs1, t.scalingMethodLs1Performance]")),
        "Scaling method order must expose Native before every scaler");
    require(!scaling_group.contains(QStringLiteral("backend.adaptive")),
        "Scaling group is coupled to Adaptive");
    require(!scaling_group.contains(
            QStringLiteral("backend.frame_generation_enabled")),
        "Scaling group is coupled to Frame Generation");
}

void test_compact_restart_markers() {
    QFile ui_file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(ui_file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString ui_qml = QString::fromUtf8(ui_file.readAll());
    require(ui_qml.count(QStringLiteral("compactRestartMarker: true")) == 7,
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

} // namespace

int main() {
    try {
        test_scaling_properties();
        test_multiplier_limits();
        test_fractional_adaptive_preset();
        test_independent_scaling_group();
        test_compact_restart_markers();
    } catch (const std::exception& error) {
        std::cerr << "mako-ui backend contract test failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
