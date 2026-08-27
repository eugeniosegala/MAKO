import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "dialogs"
import "panes"
import "widgets"

ApplicationWindow {
    property var t: localization.strings

    title: t.makoRendererConfig
    width: 900
    height: 550
    minimumWidth: 700
    minimumHeight: 400
    visible: true

    CenteredDialog {
        id: create_dialog
        name: t.createNewProfile
        onConfirm: backend.createProfile(create_name.text)

        TextField {
            id: create_name
            Layout.fillWidth: true
            placeholderText: t.chooseProfileName
            focus: true
        }
    }

    CenteredDialog {
        id: rename_dialog
        name: t.renameProfile
        onConfirm: backend.renameProfile(rename_name.text)

        TextField {
            id: rename_name
            Layout.fillWidth: true
            placeholderText: t.chooseProfileName
            focus: true
        }
    }

    CenteredDialog {
        id: delete_dialog
        name: t.confirmDeletion
        onConfirm: backend.deleteProfile()

        Label {
            Layout.fillWidth: true
            text: t.confirmDeleteMsg
            horizontalAlignment: Text.AlignHCenter
        }
    }

    LargeDialog {
        id: active_in_dialog
        name: t.activeIn

        List {
            Layout.fillWidth: true
            Layout.fillHeight: true

            model: backend.active_in
            selected: backend.active_in_index
            onSelect: index => {
                backend.active_in_index = index;
                var idx = backend.active_in.index(index, 0);
                active_in_name.text = backend.active_in.data(idx);
            }
        }

        RowLayout {
            spacing: 8

            TextField {
                id: active_in_name
                Layout.fillWidth: true
                placeholderText: t.activeInPlaceholder
                focus: true
            }
            Button {
                icon.name: "list-add"
                onClicked: backend.addActiveIn(active_in_name.text)
            }
            Button {
                icon.name: "list-remove"
                onClicked: backend.removeActiveIn()
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Pane {
            SplitView.minimumWidth: 200
            SplitView.preferredWidth: 250
            SplitView.maximumWidth: 300

            Label {
                text: t.profiles
                Layout.fillWidth: true
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            List {
                model: backend.profiles
                selected: backend.profile_index
                onSelect: index => backend.profile_index = index
            }

            Button {
                Layout.fillWidth: true
                text: t.createNewProfile
                onClicked: {
                    create_name.text = "";
                    create_dialog.open();
                }
            }
            Button {
                Layout.fillWidth: true
                text: t.renameProfile
                onClicked: {
                    var idx = backend.profiles.index(backend.profile_index, 0);
                    rename_name.text = backend.profiles.data(idx);
                    rename_dialog.open();
                }
            }
            Button {
                Layout.fillWidth: true
                text: t.deleteProfile
                onClicked: {
                    delete_dialog.open();
                }
            }
        }

        ScrollView {
            id: settings_scroll
            SplitView.fillWidth: true
            clip: true
            contentWidth: availableWidth
            leftPadding: 12
            rightPadding: 12
            topPadding: 12
            bottomPadding: 12
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: settings_scroll.availableWidth
                spacing: 12

                Group {
                    name: t.globalSettings

                    GroupEntry {
                        title: t.losslessDllPath
                        description: t.losslessDllDesc

                        FileEdit {
                            Layout.fillWidth: true

                            title: t.selectLosslessDll
                            filter: t.dllFilter

                            text: backend.dll
                            onUpdate: text => backend.dll = text
                        }
                    }

                    GroupEntry {
                        title: t.allowFp16
                        description: t.allowFp16Desc
                        enabled: !backend.ultra_performance

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.allow_fp16
                            onToggled: backend.allow_fp16 = checked
                        }
                    }
                }

                Group {
                    name: t.profileMatching
                    enabled: backend.available

                    GroupEntry {
                        title: t.activeIn
                        description: t.activeInDesc

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: backend.matched_processes.length > 0
                                    ? backend.matched_processes
                                    : t.noMatchedProcesses
                                elide: Text.ElideMiddle
                            }

                            Button {
                                text: t.editEllipsis
                                onClicked: active_in_dialog.open()
                            }
                        }
                    }
                }

                Group {
                    name: t.profileSettings
                    enabled: backend.available

                    GroupEntry {
                        title: t.frameGeneration
                        description: t.frameGenerationDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.frame_generation_enabled
                            onToggled: backend.frame_generation_enabled = checked
                        }
                    }

                    GroupEntry {
                        title: t.baseFpsCap
                        description: t.baseFpsCapDesc
                        enabled: !(backend.adaptive && backend.adaptive_auto_base_fps_cap)

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: backend.minimum_base_fps_cap
                            to: backend.maximum_base_fps_cap
                            editable: true

                            value: backend.base_fps_cap
                            textFromValue: function (value) {
                                return value === 0 ? t.off : value + t.fps;
                            }
                            valueFromText: function (text) {
                                var parsed = parseInt(text);
                                return isNaN(parsed)
                                    ? backend.minimum_base_fps_cap
                                    : Math.max(
                                        backend.minimum_base_fps_cap,
                                        Math.min(backend.maximum_base_fps_cap, parsed)
                                    );
                            }
                            onValueModified: backend.base_fps_cap = value
                        }
                    }

                    GroupEntry {
                        title: t.adaptiveFrameGen
                        description: t.adaptiveFrameGenDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.adaptive
                            onToggled: backend.adaptive = checked
                        }
                    }

                    GroupEntry {
                        title: t.targetFps
                        description: t.targetFpsDesc
                        enabled: backend.adaptive

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: backend.minimum_target_fps
                            to: backend.maximum_target_fps

                            value: backend.target_fps
                            onValueModified: backend.target_fps = value
                        }
                    }

                    GroupEntry {
                        title: t.adaptiveFpsCapPrefix + (backend.target_fps / 2) + t.adaptiveFpsCapSuffix
                        description: t.adaptiveFpsCapDesc
                        enabled: backend.adaptive

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.adaptive_auto_base_fps_cap
                            onToggled: backend.adaptive_auto_base_fps_cap = checked
                        }
                    }

                    GroupEntry {
                        title: t.maxAdaptiveMultiplier
                        description: t.maxAdaptiveMultiplierDesc
                        enabled: backend.adaptive

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: backend.minimum_adaptive_max_multiplier
                            to: backend.maximum_adaptive_max_multiplier

                            value: backend.adaptive_max_multiplier
                            textFromValue: function (value) {
                                return value + t.multiplierX;
                            }
                            valueFromText: function (text) {
                                return parseInt(text);
                            }
                            onValueModified: backend.adaptive_max_multiplier = value
                        }
                    }

                    GroupEntry {
                        title: t.smoothCadence
                        description: t.smoothCadenceDesc
                        enabled: backend.adaptive

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.adaptive_stable_cadence
                            onToggled: backend.adaptive_stable_cadence = checked
                        }
                    }

                    GroupEntry {
                        title: t.multiplier
                        description: t.multiplierDesc
                        enabled: !backend.adaptive

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: backend.minimum_multiplier
                            to: 100

                            value: backend.multiplier
                            onValueModified: backend.multiplier = value
                        }
                    }

                    GroupEntry {
                        title: t.flowScale
                        description: t.flowScaleDesc
                        enabled: !backend.ultra_performance

                        FlowSlider {
                            Layout.fillWidth: true

                            from: backend.minimum_flow_scale
                            to: backend.maximum_flow_scale

                            value: backend.flow_scale
                            onUpdate: value => backend.flow_scale = value
                        }
                    }

                    GroupEntry {
                        title: t.gpu
                        description: t.gpuDesc

                        ComboBox {
                            Layout.fillWidth: true

                            model: backend.gpus
                            currentIndex: backend.gpu
                            onActivated: index => backend.gpu = index
                        }
                    }
                }

                Group {
                    name: t.scalingSettings
                    enabled: backend.available

                    GroupEntry {
                        title: t.scalingEnabled
                        description: t.scalingEnabledDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.scaling_enabled
                            onToggled: backend.scaling_enabled = checked
                        }
                    }

                    GroupEntry {
                        title: t.scalingFactor
                        description: t.scalingFactorDesc
                        visible: backend.scaling_enabled && backend.scaling_method !== "native"
                        enabled: backend.scaling_enabled && backend.scaling_method !== "native"

                        FlowSlider {
                            Layout.fillWidth: true

                            from: backend.minimum_scaling_factor
                            to: backend.maximum_scaling_factor

                            value: backend.scaling_factor
                            onUpdate: value => backend.scaling_factor = value
                        }
                    }

                    GroupEntry {
                        title: t.scalingMethod
                        description: t.scalingMethodDesc
                        visible: backend.scaling_enabled
                        enabled: backend.scaling_enabled

                        ComboBox {
                            Layout.fillWidth: true
                            model: [t.scalingMethodNative, t.scalingMethodMako, t.scalingMethodLs1, t.scalingMethodLs1Performance]
                            currentIndex: backend.scaling_method === "native" ? 0 : backend.scaling_method === "ls1" ? 2 : backend.scaling_method === "ls1-performance" ? 3 : 1
                            onActivated: index => backend.scaling_method = index === 0 ? "native" : index === 2 ? "ls1" : index === 3 ? "ls1-performance" : "mako"
                        }
                    }

                    GroupEntry {
                        title: t.scalingSharpness
                        description: t.scalingSharpnessDesc
                        visible: backend.scaling_enabled && backend.scaling_method !== "native"
                        enabled: backend.scaling_enabled && backend.scaling_method !== "native"

                        FlowSlider {
                            Layout.fillWidth: true

                            from: backend.minimum_scaling_sharpness
                            to: backend.maximum_scaling_sharpness

                            value: backend.scaling_sharpness
                            onUpdate: value => backend.scaling_sharpness = value
                        }
                    }
                }

                Group {
                    name: t.performanceSettings
                    enabled: backend.available

                    GroupEntry {
                        title: t.ultraPerformance
                        description: t.ultraPerformanceDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.ultra_performance
                            onToggled: backend.ultra_performance = checked
                        }
                    }

                    GroupEntry {
                        title: t.performanceMode
                        description: t.performanceModeDesc
                        enabled: !backend.ultra_performance

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.performance_mode
                            onToggled: backend.performance_mode = checked
                        }
                    }
                }

                Group {
                    name: t.compatibilitySettings
                    enabled: backend.available

                    GroupEntry {
                        title: t.refreshRateGuard
                        description: t.refreshRateGuardDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.frame_generation_refresh_threshold > 0
                            onToggled: backend.frame_generation_refresh_threshold = checked
                                ? backend.frame_generation_refresh_threshold_preset
                                : 0
                        }
                    }

                    GroupEntry {
                        visible: backend.frame_generation_refresh_threshold > 0
                        title: t.refreshRateThreshold
                        description: t.refreshRateThresholdDesc

                        SpinBox {
                            Layout.alignment: Qt.AlignRight
                            from: 1
                            to: backend.maximum_frame_generation_refresh_threshold
                            editable: true

                            value: backend.frame_generation_refresh_threshold
                            textFromValue: function (value) {
                                return value + " Hz";
                            }
                            valueFromText: function (text) {
                                var parsed = parseInt(text);
                                return isNaN(parsed) ? 1 : Math.max(
                                    1,
                                    Math.min(
                                        backend.maximum_frame_generation_refresh_threshold,
                                        parsed
                                    )
                                );
                            }
                            onValueModified: backend.frame_generation_refresh_threshold = value
                        }
                    }

                    GroupEntry {
                        title: t.dynamicCadence
                        description: t.dynamicCadenceDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.dynamic_cadence_recovery
                            onToggled: backend.dynamic_cadence_recovery = checked
                        }
                    }

                    GroupEntry {
                        visible: backend.dynamic_cadence_recovery
                        title: t.dynamicCadenceProbeInterval
                        description: t.dynamicCadenceProbeIntervalDesc

                        ComboBox {
                            id: dynamic_cadence_probe_interval
                            Layout.fillWidth: true
                            model: backend.dynamic_cadence_probe_interval_presets_seconds
                            currentIndex: {
                                for (let index = 0; index < model.length; ++index) {
                                    if (Math.abs(Number(model[index]) - backend.dynamic_cadence_probe_interval_seconds) < 0.001)
                                        return index
                                }
                                return -1
                            }
                            displayText: currentIndex >= 0 ? Number(Number(model[currentIndex]).toFixed(2)) + t.secondsSuffix : Number(backend.dynamic_cadence_probe_interval_seconds.toFixed(2)) + t.secondsSuffix
                            onActivated: backend.dynamic_cadence_probe_interval_seconds = Number(model[index])
                        }
                    }
                }

                Group {
                    name: t.standaloneLaunchSettings

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: t.standaloneLaunchDesc
                        color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.7)
                    }

                    GroupEntry {
                        title: t.enableZink
                        description: t.enableZinkDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight
                            checked: backend.enable_zink
                            onToggled: backend.enable_zink = checked
                        }
                    }

                    GroupEntry {
                        title: t.forceAlsaAudio
                        description: t.forceAlsaAudioDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight
                            checked: backend.force_alsa_audio
                            onToggled: backend.force_alsa_audio = checked
                        }
                    }
                }

                Group {
                    name: t.deckyIntegration

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: t.deckyDesc
                        color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.7)
                    }
                }

                Group {
                    name: t.interfaceSettings

                    GroupEntry {
                        title: t.language
                        description: t.languageDesc

                        ComboBox {
                            Layout.fillWidth: true

                            model: localization.language_names
                            currentIndex: localization.language_index
                            onActivated: index => localization.language_index = index
                        }
                    }
                }
            }
        }
    }
}
